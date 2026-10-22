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
	return 0;
}
