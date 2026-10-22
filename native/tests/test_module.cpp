// SPDX-License-Identifier: GPL-3.0-or-later
// The construction state machine, without Python or a fake Rack host.
#include "../module/module.hpp"

#include <cassert>
#include <thread>

using rackpy::module::Construction;
using namespace rackpy::runtime;

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
	return 0;
}
