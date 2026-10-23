// SPDX-License-Identifier: GPL-3.0-or-later
// The construction state machine, without Python or a fake Rack host.
#include "../module/module.hpp"

#include <cassert>
#include <thread>

using rackpy::module::Construction;
using namespace rackpy::runtime;

/** Stands in for Python-created metadata: retiring hands it back, not delete. */
struct Retiring : rack::engine::PortInfo, rackpy::module::TransferredMetadata {
	std::vector<std::string>* log;
	bool inSlot = true;
	explicit Retiring(std::vector<std::string>* log) : log(log) {}
	bool transferred() const override { return inSlot; }
	void retire() noexcept override {
		log->push_back("retired " + name + (module ? " linked" : ""));
		delete this;
	}
};

template <typename Error, typename F>
void refuses(F action) {
	bool caught = false;
	try { action(); }
	catch (const Error&) { caught = true; }
	assert(caught);
}

int main() {
	markMainThread();
	{
		Construction c;
		refuses<std::invalid_argument>([&] { c.config(-1, 0, 0, 0); });
		c.config(0, 0, 0, 0);
		refuses<Ownership>([&] { c.config(0, 0, 0, 0); });
		refuses<Ownership>([&] { c.close(); });
		c.seal();
		refuses<Ownership>([&] { c.config(0, 0, 0, 0); });
		auto token = c.get().token;
		c.close();
		c.close();
		assert(!token->alive());
		refuses<StaleHandle>([&] { c.get(); });
	}
	{
		Construction c;
		c.config(1, 2, 2, 1);
		refuses<std::out_of_range>([&] { c.configBypass(-1, 0); });
		refuses<std::out_of_range>([&] { c.configBypass(0, 2); });
		c.configBypass(0, 0);
		refuses<std::invalid_argument>([&] { c.configBypass(1, 0); });
		c.configBypass(0, 1);
		auto token = c.get().token;
		c.abort();
		assert(!token->alive());
		refuses<StaleHandle>([&] { c.seal(); });
	}
	{
		Construction c;
		c.config(1, 1, 1, 1);
		c.seal();
		std::thread worker([&] {
			refuses<WrongThread>([&] { c.get(); });
			refuses<WrongThread>([&] { c.close(); });
			refuses<WrongThread>([&] { c.config(0, 0, 0, 0); });
		});
		worker.join();
		assert(c.get().getNumParams() == 1);
	}
	{
		Construction c;
		refuses<std::out_of_range>([&] { c.configPortInfo(0, "", true); });
		c.config(0, 1, 1, 1);
		auto& m = c.get();
		assert(m.getLightInfo(0) == nullptr);
		auto oldToken = m.inputInfoTokens[0];
		auto old = m.getInputInfo(0);
		// Exercise preparation failure deterministically, without allocation
		// fault injection or a Python/testing hook in production bindings.
		refuses<std::runtime_error>([&] {
			c.replaceInfo(0, m.inputInfos, m.inputInfoTokens, [](rack::engine::PortInfo&) {
				throw std::runtime_error("preparation failed");
			});
		});
		assert(m.getInputInfo(0) == old && oldToken->alive());
		assert(m.inputInfoTokens[0] == oldToken);
		c.configPortInfo(0, "Pitch", true);
		assert(!oldToken->alive());
		assert(m.getInputInfo(0)->module == &m);
		assert(m.getInputInfo(0)->type == rack::engine::Port::INPUT);
		assert(m.getInputInfo(0)->portId == 0);
		assert(m.getInputInfo(0)->getName() == "Pitch");
		c.configPortInfo(0, "Signal", false);
		assert(m.getOutputInfo(0)->type == rack::engine::Port::OUTPUT);
		c.configLightInfo(0, "Activity");
		assert(m.getLightInfo(0)->module == &m && m.getLightInfo(0)->lightId == 0);
		auto input = m.inputInfoTokens[0], output = m.outputInfoTokens[0], light = m.lightInfoTokens[0];
		refuses<std::out_of_range>([&] { c.configPortInfo(-1, "", true); });
		refuses<std::out_of_range>([&] { c.configLightInfo(1, ""); });
		assert(input->alive() && output->alive() && light->alive());
		c.seal();
		refuses<Ownership>([&] { c.configPortInfo(0, "", true); });
		refuses<Ownership>([&] { c.configLightInfo(0, ""); });
		std::thread worker([&] {
			refuses<WrongThread>([&] { c.configPortInfo(0, "", false); });
			refuses<WrongThread>([&] { c.configLightInfo(0, ""); });
		});
		worker.join();
		c.close();
		assert(!input->alive() && !output->alive() && !light->alive());
	}
	{
		Construction c;
		c.config(2, 0, 0, 0);
		auto& m = c.get();
		auto old = m.getParamQuantity(0);
		auto token = m.paramQuantityTokens[0];
		refuses<std::runtime_error>([&] {
			c.replaceInfo(0, m.paramQuantities, m.paramQuantityTokens, [](rack::engine::ParamQuantity&) {
				throw std::runtime_error("quantity preparation failed");
			});
		});
		assert(m.getParamQuantity(0) == old && token->alive() && m.params[0].value == 0.f);
		refuses<std::invalid_argument>([&] { c.configQuantity(0, 1, 1, 1, "", "", 0, 1, 0); });
		assert(m.getParamQuantity(0) == old && token->alive());
		c.configQuantity(0, -1, 1, 0.5f, "Pitch", " V", 0, 2, 3);
		assert(!token->alive() && m.params[0].value == 0.5f);
		assert(m.getParamQuantity(0)->module == &m && m.getParamQuantity(0)->paramId == 0);
		c.configQuantity(1, 0, 1, 0, "Trigger", "", 0, 1, 0, 2);
		auto button = dynamic_cast<rack::engine::SwitchQuantity*>(m.getParamQuantity(1));
		assert(button && button->snapEnabled && !button->smoothEnabled && !button->randomizeEnabled);
		auto last = m.paramQuantityTokens[1];
		c.seal();
		refuses<Ownership>([&] { c.configQuantity(0, 0, 1, 0, "", "", 0, 1, 0); });
		c.close();
		assert(!last->alive());
	}
	{
		Construction first, second;
		rack::engine::PortInfo port;
		rack::engine::LightInfo light;
		auto portLink = first.get().trackBackreference(&port.module);
		auto lightLink = first.get().trackBackreference(&light.module);
		port.module = light.module = &first.get();
		// Retire the old subscription before pointing at a different module.
		portLink->detach();
		portLink = second.get().trackBackreference(&port.module);
		port.module = &second.get();
		first.abort();
		assert(light.module == nullptr && port.module == &second.get());
		second.abort();
		assert(port.module == nullptr);
	}
	{
		Construction c;
		rack::engine::PortInfo port;
		std::shared_ptr<rackpy::module::ModuleBackreference> link;
		for (int i = 0; i < 1000; ++i) {
			if (link)
				link->detach();
			link = c.get().trackBackreference(&port.module);
			port.module = &c.get();
			assert(c.get().backreferences.size() <= 2);
		}
		link->detach();
		link.reset();
		auto last = c.get().trackBackreference(&port.module);
		assert(c.get().backreferences.size() == 1);
		c.abort();
		assert(port.module == nullptr);
	}
	// RP-14B2b5: transferred metadata retires on replacement and on module
	// deletion, before Rack's base destructor would delete the slot.
	{
		std::vector<std::string> log;
		Construction c;
		c.config(0, 1, 1, 1);
		auto& m = c.get();
		auto token = m.inputInfoTokens[0];
		auto* builtin = m.getInputInfo(0);
		// A failing factory (as a raising Python constructor) changes nothing.
		refuses<std::runtime_error>([&] {
			c.configPortInfo(0, "Never", true, []() -> std::unique_ptr<rack::engine::PortInfo> {
				throw std::runtime_error("factory failed");
			});
		});
		assert(m.getInputInfo(0) == builtin && token->alive());
		c.configPortInfo(0, "First", true, [&] { return std::make_unique<Retiring>(&log); });
		assert(!token->alive() && log.empty());
		auto* first = m.getInputInfo(0);
		assert(first->module == &m && first->portId == 0 && first->type == rack::engine::Port::INPUT);
		c.configPortInfo(0, "Second", true, [&] { return std::make_unique<Retiring>(&log); });
		assert((log == std::vector<std::string>{"retired First linked"}));
		c.configPortInfo(0, "Plain", true);
		assert(log.size() == 2 && log[1] == "retired Second linked");
		c.configPortInfo(0, "Kept", false, [&] { return std::make_unique<Retiring>(&log); });
		c.configLightInfo(0, "Light");
		// An object a slot no longer claims is native metadata: delete it.
		auto* released = new Retiring(&log);
		released->inSlot = false;
		assert(rackpy::module::transferredIn(static_cast<rack::engine::PortInfo*>(released)) == nullptr);
		delete released;
		c.seal();
		c.close();
		assert(log.size() == 3 && log[2] == "retired Kept linked");
	}
	// Native deletion of standalone metadata can race main-thread module
	// cleanup. detach completes before the actual SDK field ceases to exist.
	for (int i = 0; i < 100; ++i) {
		Construction c;
		auto info = std::make_unique<rack::engine::PortInfo>();
		info->module = &c.get();
		auto link = c.get().trackBackreference(&info->module);
		std::thread worker([&] { link->detach(); info.reset(); });
		c.abort();
		worker.join();
	}
	return 0;
}
