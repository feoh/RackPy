// SPDX-License-Identifier: GPL-3.0-or-later
// rack::componentlibrary's concrete widgets (RP-28C), shared by the
// components_*.cpp translation units, which are split only to compile in
// parallel. See components.cpp.
#pragma once
#include <rack.hpp>

#include "app_internal.hpp"

namespace cl = rack::componentlibrary;

namespace pybind11 {
#define RACKPY_CL_ADDRESS(T) \
	template <> \
	struct polymorphic_type_hook<cl::T> { \
		static const void* get(const cl::T* ptr, const std::type_info*& type) { \
			type = &typeid(cl::T); \
			return ptr; \
		} \
	};
#define RACKPY_CL_KNOBS(X) \
	X(RoundBlackKnob, cl::RoundKnob) X(RoundSmallBlackKnob, cl::RoundKnob) \
	X(RoundLargeBlackKnob, cl::RoundKnob) X(RoundBigBlackKnob, cl::RoundKnob) X(RoundHugeBlackKnob, cl::RoundKnob) \
	X(RoundBlackSnapKnob, cl::RoundBlackKnob) \
	X(Davies1900hWhiteKnob, cl::Davies1900hKnob) X(Davies1900hBlackKnob, cl::Davies1900hKnob) \
	X(Davies1900hRedKnob, cl::Davies1900hKnob) X(Davies1900hLargeWhiteKnob, cl::Davies1900hKnob) \
	X(Davies1900hLargeBlackKnob, cl::Davies1900hKnob) X(Davies1900hLargeRedKnob, cl::Davies1900hKnob)
#define RACKPY_CL_ROGANS(X) \
	X(Rogan6PSWhite, cl::Rogan) X(Rogan5PSGray, cl::Rogan) X(Rogan3PSBlue, cl::Rogan) \
	X(Rogan3PSRed, cl::Rogan) X(Rogan3PSGreen, cl::Rogan) X(Rogan3PSWhite, cl::Rogan) X(Rogan3PBlue, cl::Rogan) \
	X(Rogan3PRed, cl::Rogan) X(Rogan3PGreen, cl::Rogan) X(Rogan3PWhite, cl::Rogan) X(Rogan2SGray, cl::Rogan) \
	X(Rogan2PSBlue, cl::Rogan) X(Rogan2PSRed, cl::Rogan) X(Rogan2PSGreen, cl::Rogan)
#define RACKPY_CL_ROGANS2(X) \
	X(Rogan2PSWhite, cl::Rogan) X(Rogan2PBlue, cl::Rogan) X(Rogan2PRed, cl::Rogan) X(Rogan2PGreen, cl::Rogan) \
	X(Rogan2PWhite, cl::Rogan) X(Rogan1PSBlue, cl::Rogan) X(Rogan1PSRed, cl::Rogan) X(Rogan1PSGreen, cl::Rogan) \
	X(Rogan1PSWhite, cl::Rogan) X(Rogan1PBlue, cl::Rogan) X(Rogan1PRed, cl::Rogan) X(Rogan1PGreen, cl::Rogan) \
	X(Rogan1PWhite, cl::Rogan)
#define RACKPY_CL_SWITCHES(X) \
	X(BefacoSlidePot, rack::app::SvgSlider) X(VCVSlider, rack::app::SvgSlider) \
	X(NKK, rack::app::SvgSwitch) X(CKSS, rack::app::SvgSwitch) X(CKSSThree, rack::app::SvgSwitch) \
	X(CKSSThreeHorizontal, rack::app::SvgSwitch) X(CKD6, rack::app::SvgSwitch) X(TL1105, rack::app::SvgSwitch) \
	X(VCVButton, rack::app::SvgSwitch) X(VCVLatch, cl::VCVButton) X(BefacoSwitch, rack::app::SvgSwitch) \
	X(BefacoPush, rack::app::SvgSwitch) X(VCVBezel, rack::app::SvgSwitch) X(VCVBezelLatch, cl::VCVBezel) \
	X(PB61303, rack::app::SvgSwitch)
#define RACKPY_CL_PORTS(X) \
	X(PJ301MPort, rack::app::SvgPort) X(DarkPJ301MPort, rack::app::SvgPort) X(ThemedPJ301MPort, rack::app::ThemedSvgPort) \
	X(PJ3410Port, rack::app::SvgPort) X(CL1362Port, rack::app::SvgPort)
#define RACKPY_CL_OTHERS(X) \
	X(ScrewSilver, rack::app::SvgScrew) X(ScrewBlack, rack::app::SvgScrew) X(ThemedScrew, rack::app::ThemedSvgScrew) \
	X(SegmentDisplay, rack::widget::Widget)
#define RACKPY_CL_BG_KNOBS(X) \
	X(RoundKnob) X(Davies1900hKnob) X(Rogan) X(SynthTechAlco) X(Trimpot) X(BefacoBigKnob) X(BefacoTinyKnob)
#define RACKPY_CL_HOOK(T, P) RACKPY_CL_ADDRESS(T)
#define RACKPY_CL_HOOK1(T) RACKPY_CL_ADDRESS(T)
RACKPY_CL_BG_KNOBS(RACKPY_CL_HOOK1)
#undef RACKPY_CL_HOOK1
RACKPY_CL_KNOBS(RACKPY_CL_HOOK)
RACKPY_CL_ROGANS(RACKPY_CL_HOOK)
RACKPY_CL_ROGANS2(RACKPY_CL_HOOK)
RACKPY_CL_SWITCHES(RACKPY_CL_HOOK)
RACKPY_CL_PORTS(RACKPY_CL_HOOK)
RACKPY_CL_OTHERS(RACKPY_CL_HOOK)
#undef RACKPY_CL_HOOK
#undef RACKPY_CL_ADDRESS
}  // namespace pybind11

namespace rackpy {
namespace widgets {
namespace appdetail {
void bindComponentKnobs(py::module_ mod);
void bindComponentRogans(py::module_ mod);
void bindComponentRogans2(py::module_ mod);
void bindComponentSwitches(py::module_ mod);
void bindComponentPorts(py::module_ mod);

/** A param component bound with its parent class (the parent first). */
#define RACKPY_CL_PARAM(T, P) \
	{ auto cls = variant<cl::T, P, ParamWidgetT<cl::T>>(mod, #T); paramMethods(cls); }

template <class T, class... Options>
void svgChild(py::classh<T, Options...>& cls, const char* name, rack::widget::SvgWidget* T::*field) {
	cls.def_property_readonly(name, [field](py::object self) { return wrapper(native<T>(self).*field); },
		"Rack's own SvgWidget child (a HostSvgWidget), created in the constructor.");
}
}  // namespace appdetail
}  // namespace widgets
}  // namespace rackpy
