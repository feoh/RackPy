// SPDX-License-Identifier: GPL-3.0-or-later
// Native unit tests for the runtime's Python-free parts: thread roles, the
// exclusive-lock guard, context binding and leases, object tokens and the
// lock-free diagnostics ring. Runs outside Rack, linked against the SDK's
// libRack; tests/runtime checks the same rules inside a real Rack.
#include <rack.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "rackpy/runtime/context.hpp"
#include "rackpy/runtime/diagnostics.hpp"
#include "rackpy/runtime/threads.hpp"

using namespace rackpy::runtime;

namespace {

int failures = 0;

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			failures++; \
		} \
	} while (0)

template <typename E, typename F>
bool throwsWith(F&& f, const std::string& text) {
	try {
		f();
	}
	catch (const E& e) {
		if (std::string(e.what()).find(text) != std::string::npos)
			return true;
		std::fprintf(stderr, "exception text \"%s\" lacks \"%s\"\n", e.what(), text.c_str());
		return false;
	}
	catch (...) {
		std::fprintf(stderr, "a different exception type was thrown\n");
		return false;
	}
	return false;
}

template <typename F>
auto onThread(F&& f) -> decltype(f()) {
	decltype(f()) result{};
	std::thread t([&] { result = f(); });
	t.join();
	return result;
}

// Bare contexts: never deleted, because ~Context tears down whatever it holds. They stay reachable
// from here, so that LeakSanitizer (RP-38's sanitizer build) does not report them as leaks.
rack::Context* newContext() {
	static auto* contexts = new std::vector<rack::Context*>;  // never destroyed: still reachable at exit
	contexts->push_back(new rack::Context);
	return contexts->back();
}

void testThreadRoles() {
	markMainThread();
	CHECK(isMainThread());
	CHECK(threadRole() == ThreadRole::Main);
	CHECK(pythonAllowedHere());
	CHECK(onThread([] { return threadRole() == ThreadRole::Other; }));
	CHECK(onThread([] { return pythonAllowedHere(); }));
	rack::Context* context = newContext();
	// A thread with a Rack context that is not the main thread is one of Rack's (engine, driver).
	CHECK(onThread([context] {
		rack::contextSet(context);
		return threadRole() == ThreadRole::Rack && !pythonAllowedHere();
	}));
	CHECK(onThread([] { return throwsWith<WrongThread>([] { requireMainThread("add_module"); }, "add_module must be called on Rack's main (UI) thread, not on a thread Rack did not start"); }));
	requireMainThread("on the main thread");  // does not throw
}

void testExclusiveGuard() {
	CHECK(!insideExclusive());
	refuseEngineReentry("outside");  // does not throw
	{
		ExclusiveScope outer;
		{
			ExclusiveScope inner;
			CHECK(insideExclusive());
		}
		CHECK(insideExclusive());
		CHECK(throwsWith<EngineLocked>([] { refuseEngineReentry("getModuleIds"); }, "getModuleIds takes the engine lock"));
		// The guard is per thread.
		CHECK(onThread([] { return !insideExclusive(); }));
	}
	CHECK(!insideExclusive());
}

void testContextScopeRestores() {
	rack::Context* a = newContext();
	rack::Context* b = newContext();
	rack::contextSet(a);
	{
		ContextScope scope(b);
		CHECK(rack::contextGet() == b);
	}
	CHECK(rack::contextGet() == a);
	rack::contextSet(nullptr);
}

void testBinding() {
	ContextBinding binding;
	rack::Context* first = newContext();
	rack::Context* second = newContext();
	CHECK(!binding.observe(nullptr));
	std::shared_ptr<ContextToken> token = binding.observe(first);
	CHECK(token && token->context == first && token->alive());
	CHECK(binding.observe(first) == token);
	CHECK(!binding.observe(second));
	CHECK(!binding.observe(second));
	CHECK(binding.refusedContexts() == 2);
	CHECK(binding.current() == token);

	// A Python thread holds an engine lease; close() waits for it before marking the context dead.
	std::atomic<bool> holding{false};
	std::thread holder([&] {
		EngineLease lease(*token);
		holding.store(true);
		std::this_thread::sleep_for(std::chrono::milliseconds(60));
	});
	while (!holding.load())
		std::this_thread::yield();
	auto t0 = std::chrono::steady_clock::now();
	binding.close();
	double waited = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	holder.join();
	CHECK(waited >= 40);
	CHECK(!token->alive());
	CHECK(!binding.current());
	binding.close();  // idempotent
	CHECK(throwsWith<StaleHandle>([&] { EngineLease lease(*token); }, "has closed"));
	// A closed context stays refused, even at the same address: it would be a new generation.
	CHECK(!binding.observe(first));
	binding.reset();
	CHECK(!binding.current());
}

void testCheckContext() {
	ContextBinding binding;
	rack::Context* bound = newContext();
	rack::Context* other = newContext();
	std::shared_ptr<ContextToken> token = binding.observe(bound);
	checkContext(*token);  // no context active on this thread: allowed
	rack::contextSet(bound);
	checkContext(*token);
	rack::contextSet(other);
	CHECK(throwsWith<WrongContext>([&] { checkContext(*token); }, "different Rack context"));
	rack::contextSet(nullptr);
	binding.close();
	CHECK(throwsWith<StaleHandle>([&] { checkContext(*token); }, "has closed"));
}

void testObjectTokensAndHandles() {
	ContextBinding binding;
	std::shared_ptr<ContextToken> context = binding.observe(newContext());
	int object = 42;
	Borrowed<int> handle{&object, std::make_shared<ObjectToken>(context), "test object"};
	Borrowed<int> other{&object, std::make_shared<ObjectToken>(context), "test object"};
	CHECK(handle.get() == 42 && handle.alive());
	CHECK(handle.generation() != other.generation());  // same address, different generations
	handle.token->invalidate();
	CHECK(!handle.alive());
	CHECK(throwsWith<StaleHandle>([&] { handle.get(); }, "this test object has been deleted by Rack"));
	CHECK(other.get() == 42);
	binding.close();
	CHECK(throwsWith<StaleHandle>([&] { other.get(); }, "belonged to a Rack context that has closed"));
	Borrowed<int> empty;
	CHECK(throwsWith<StaleHandle>([&] { empty.get(); }, "has been deleted"));
}

void testDiagnosticsRing() {
	uint64_t before = diagnosticsRecorded();
	uint64_t testsBefore = diagnosticCount(DiagnosticCode::Test);
	const int threads = 8;
	const int each = 1000;
	std::vector<std::thread> writers;
	for (int t = 0; t < threads; t++) {
		writers.emplace_back([t] {
			for (int i = 0; i < each; i++) {
				char message[32];
				std::snprintf(message, sizeof(message), "t%d n%d", t, i);
				diagnose(DiagnosticCode::Test, message);
			}
		});
	}
	for (auto& w : writers)
		w.join();
	CHECK(diagnosticsRecorded() - before == (uint64_t) threads * each);
	CHECK(diagnosticCount(DiagnosticCode::Test) - testsBefore == (uint64_t) threads * each);
	std::vector<Diagnostic> kept = readDiagnostics(before);
	CHECK(!kept.empty() && kept.size() <= DIAGNOSTIC_CAPACITY);
	uint64_t last = 0;
	for (const Diagnostic& d : kept) {
		CHECK(d.sequence > last);
		last = d.sequence;
		int t = -1, i = -1;
		CHECK(std::sscanf(d.message.c_str(), "t%d n%d", &t, &i) == 2 && t >= 0 && t < threads && i >= 0 && i < each);
		CHECK(d.code == DiagnosticCode::Test);
	}
	CHECK(last == diagnosticsRecorded());

	std::string longText(500, 'x');
	diagnose(DiagnosticCode::CallbackRaised, longText.c_str());
	std::vector<Diagnostic> newest = readDiagnostics(diagnosticsRecorded() - 1);
	CHECK(newest.size() == 1 && newest[0].message == std::string(DIAGNOSTIC_MESSAGE_BYTES - 1, 'x'));
	CHECK(std::string(codeName(newest[0].code)) == "callback_raised");
	CHECK(newest[0].role == ThreadRole::Main);
	CHECK(readDiagnostics(diagnosticsRecorded()).empty());
}

}  // namespace

int main() {
	const std::function<void()> tests[] = {testThreadRoles, testExclusiveGuard, testContextScopeRestores, testBinding,
		testCheckContext, testObjectTokensAndHandles, testDiagnosticsRing};
	for (const auto& t : tests)
		t();
	if (failures) {
		std::fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	std::printf("runtime core tests passed\n");
	return 0;
}
