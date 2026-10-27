// SPDX-License-Identifier: GPL-3.0-or-later
// RP-23C: expander exchange under stress, built twice by CMake: with
// AddressSanitizer and UndefinedBehaviorSanitizer, and with ThreadSanitizer
// (tests/engine/test_expander_stress.py runs both).
//
// A row of RackPy modules runs graph plans that send a counter to the right
// and receive from the left, stepped as Rack's engine steps them: worker
// threads process the modules of a frame between two barriers, then the
// engine thread flips the requested messages (Engine_stepFrame). Between
// blocks, with the workers parked as Rack's exclusive engine lock parks them,
// the main thread removes modules (deleting them), inserts new ones,
// reorders the row, gives a module messages of another schema, and turns
// snapshots on and off; while the engine runs, it reads snapshots, as Python
// does. After every change it checks what each receiver got: the left
// neighbour's counter of the previous frame once a pair has been in place for
// two frames, and silence beside a foreign or mismatched module.
#include "../dsp/graph/plan.hpp"
#include "../engine/expanders.hpp"
#include "../module/module.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

using namespace rackpy;

namespace {

int failures = 0;

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			failures++; \
		} \
	} while (0)

/** Sends a counter (frame n: n + 1) right on lane 0 as 16 equal channels (input 0, patched with 16 channels of
0 V, plus the counter), outputs the counter on output 1, and outputs what arrives from the left on lane 0 on
output 0. Equal channels make a torn snapshot visible. */
std::unique_ptr<dsp::Hosted> chainPlan() {
	graph::PlanSpec spec;
	auto node = [&](const char* op, std::vector<long long> inputs = {}, std::vector<double> args = {}) {
		spec.nodes.push_back({op, std::move(inputs), std::move(args), {}});
		return (long long) spec.nodes.size() - 1;
	};
	long long count = node("delay", {}, {1});
	spec.latches.emplace_back(count, node("add", {count, node("constant", {}, {1})}));
	node("expander_send", {node("add", {node("input", {}, {0}), count})}, {1, 0});
	node("output", {count}, {1});
	node("output", {node("expander_receive", {}, {0, 0})}, {0});
	auto type = graph::planType(spec);
	auto hosted = std::make_unique<dsp::Hosted>();
	hosted->type = type.get();
	hosted->ownedType = type;
	hosted->values = graph::planDefaults(*type);
	hosted->kernel = type->create(hosted->values);
	return hosted;
}

int64_t nextId = 1;

/** A RackPy module of the row, with "rackpy.graph" messages on its left (or another schema). */
std::unique_ptr<module::Module> makeModule(bool mismatched = false) {
	auto m = std::make_unique<module::Module>();
	m->id = nextId++;
	m->config(0, 1, 2, 0);
	m->inputs[0].channels = 16;
	for (auto& o : m->outputs)
		o.channels = 1;
	const char* schema = mismatched ? "demo.Other" : engine::GRAPH_SCHEMA;
	auto a = engine::allocateMessage(schema, graph::LANE), b = engine::allocateMessage(schema, graph::LANE);
	m->leftExpander.producerMessage = a->data;
	m->leftExpander.consumerMessage = b->data;
	m->expanderMessages.push_back(a);
	m->expanderMessages.push_back(b);
	engine::publishChannel(*m, *m, 0);
	m->kernels.attach(chainPlan(), false);
	return m;
}

/** One slot of the row: a RackPy module or another plugin's (which never exchanges). */
struct Slot {
	std::unique_ptr<module::Module> own;
	std::unique_ptr<rack::engine::Module> foreign;
	bool mismatched = false;
	int64_t placedAt = 0;  // the frame from which its left neighbour has been the same
	rack::engine::Module* get() const { return own ? (rack::engine::Module*) own.get() : foreign.get(); }
};

/** A barrier for a fixed number of threads, as Rack's engine uses between the steps of a frame. */
struct Barrier {
	std::mutex mutex;
	std::condition_variable cv;
	int count = 0, total, generation = 0;
	explicit Barrier(int total) : total(total) {}
	void wait() {
		std::unique_lock<std::mutex> lock(mutex);
		int g = generation;
		if (++count == total) {
			count = 0;
			generation++;
			cv.notify_all();
			return;
		}
		cv.wait(lock, [&] { return generation != g; });
	}
};

struct Engine {
	std::vector<Slot> row;
	std::vector<rack::engine::Module*> modules;  // the row, as the workers see it during a block
	int workers;
	Barrier start, done;
	std::atomic<bool> stopping{false};
	int64_t frame = 0;
	std::vector<std::thread> threads;

	explicit Engine(int workers) : workers(workers), start(workers + 1), done(workers + 1) {}

	/** As RackWidget::updateExpanders and Engine_updateExpander_NoLock: IDs and pointers from the row's order. */
	void relink() {
		for (size_t i = 0; i < row.size(); i++) {
			rack::engine::Module* m = row[i].get();
			rack::engine::Module* left = i > 0 ? row[i - 1].get() : nullptr;
			rack::engine::Module* right = i + 1 < row.size() ? row[i + 1].get() : nullptr;
			if (m->leftExpander.module != left || m->leftExpander.moduleId != (left ? left->id : -1))
				row[i].placedAt = frame;  // a new neighbour (a new module may reuse a deleted one's address)
			m->leftExpander.moduleId = left ? left->id : -1;
			m->leftExpander.module = left;
			m->rightExpander.moduleId = right ? right->id : -1;
			m->rightExpander.module = right;
		}
		modules.clear();
		for (auto& s : row)
			modules.push_back(s.get());
	}

	void work(int index) {
		rack::engine::Module::ProcessArgs args;
		args.sampleRate = 48000.f;
		args.sampleTime = 1.f / 48000.f;
		while (true) {
			start.wait();
			if (stopping.load())
				return;
			args.frame = frame;
			for (size_t i = (size_t) index; i < modules.size(); i += (size_t) workers)
				modules[i]->process(args);
			done.wait();
		}
	}

	void launch() {
		for (int k = 0; k < workers; k++)
			threads.emplace_back([this, k] { work(k); });
	}

	/** Engine_stepFrame: the modules on the workers, then the flips on this thread. */
	void stepFrame() {
		start.wait();
		done.wait();
		for (rack::engine::Module* m : modules)
			for (rack::engine::Module::Expander* e : {&m->leftExpander, &m->rightExpander})
				if (e->messageFlipRequested) {
					std::swap(e->producerMessage, e->consumerMessage);
					e->messageFlipRequested = false;
				}
		frame++;
	}

	void stop() {
		stopping.store(true);
		start.wait();
		for (auto& t : threads)
			t.join();
	}
};

/** What each receiver got in the last frame, checked after a block. */
void checkRow(const Engine& engine) {
	for (size_t i = 0; i < engine.row.size(); i++) {
		const Slot& s = engine.row[i];
		if (!s.own)
			continue;
		float got = s.own->outputs[0].getVoltage();
		const Slot* left = i > 0 ? &engine.row[i - 1] : nullptr;
		if (!left || !left->own || s.mismatched) {
			CHECK(got == 0.f);  // silence: no RackPy neighbour, or messages of another schema
			continue;
		}
		if (engine.frame - s.placedAt < 2)
			continue;  // the first frame may still deliver what the previous neighbour sent
		// The neighbour's counter of the previous frame: its own counter output now, less one.
		CHECK(got == left->own->outputs[1].getVoltage() - 1.f);
	}
}

}  // namespace

int main(int argc, char** argv) {
	runtime::markMainThread();
	const int workers = argc > 1 ? std::atoi(argv[1]) : 4;
	const int blocks = argc > 2 ? std::atoi(argv[2]) : 400;
	std::mt19937 rng(20261026);
	Engine engine(workers);
	for (int k = 0; k < 8; k++)
		engine.row.push_back(Slot{makeModule(), nullptr});
	engine.relink();
	engine.launch();

	// Python's side: reads snapshots while the engine runs (and turns them on and off between blocks).
	std::atomic<bool> reading{true};
	std::mutex rowMutex;  // held by the main thread while it changes the row; the reader skips meanwhile
	std::vector<module::Module*> readable;
	auto collect = [&] {
		readable.clear();
		for (auto& s : engine.row)
			if (s.own)
				readable.push_back(s.own.get());
	};
	collect();
	std::atomic<long> snapshotsRead{0}, inconsistent{0};
	std::thread reader([&] {
		std::vector<float> values;
		std::mt19937 local(7);
		while (reading.load()) {
			std::lock_guard<std::mutex> lock(rowMutex);
			if (readable.empty())
				continue;
			module::Module* m = readable[local() % readable.size()];
			if (const engine::Snapshot* snapshot = m->snapshots[0].get()) {
				int64_t at;
				if (snapshot->read(at, values)) {
					snapshotsRead++;
					// Before the first message, nothing; then always 16 equal channels, never a torn mixture.
					bool equal = std::all_of(values.begin() + 1, values.end(), [&](float v) { return v == values[1]; });
					if (!((values[0] == 0.f || values[0] == 16.f) && equal))
						inconsistent++;
				}
			}
		}
	});

	int changes = 0;
	for (int b = 0; b < blocks; b++) {
		int frames = 1 + (int) (rng() % 64);
		for (int f = 0; f < frames; f++)
			engine.stepFrame();
		// Rack's exclusive lock: the workers are parked at the start barrier, and the reader is held off.
		{
			std::lock_guard<std::mutex> lock(rowMutex);
			checkRow(engine);
			switch (rng() % 6) {
				case 0:  // remove a module (Rack's removeModule, then delete)
					if (engine.row.size() > 3) {
						size_t i = rng() % engine.row.size();
						engine.row.erase(engine.row.begin() + (long) i);
					}
					break;
				case 1:  // insert a new RackPy module
					engine.row.insert(engine.row.begin() + (long) (rng() % (engine.row.size() + 1)), Slot{makeModule(), nullptr});
					break;
				case 2:  // reorder: swap two modules
					std::swap(engine.row[rng() % engine.row.size()], engine.row[rng() % engine.row.size()]);
					break;
				case 3: {  // another plugin's module, or a RackPy module whose messages have another schema
					Slot s;
					if (rng() % 2) {
						s.foreign = std::make_unique<rack::engine::Module>();
						s.foreign->id = nextId++;
					}
					else {
						s.own = makeModule(true);
						s.mismatched = true;
					}
					engine.row.insert(engine.row.begin() + (long) (rng() % (engine.row.size() + 1)), std::move(s));
					break;
				}
				case 4:  // snapshots on or off
					for (auto& s : engine.row)
						if (s.own)
							s.own->snapshotsWanted[0].store(rng() % 2);
					break;
				default: break;  // keep running unchanged
			}
			engine.relink();
			collect();
			changes++;
		}
	}
	for (int f = 0; f < 3; f++)
		engine.stepFrame();
	checkRow(engine);
	reading.store(false);
	reader.join();
	engine.stop();
	engine.row.clear();
	CHECK(engine::messageCount() == 0);  // every buffer freed with its module
	CHECK(inconsistent.load() == 0);
	std::printf("expander stress: %d workers, %lld frames, %d changes, %ld snapshots read\n", workers,
		(long long) engine.frame, changes, snapshotsRead.load());
	if (failures) {
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("expander stress: all checks passed\n");
	return 0;
}
