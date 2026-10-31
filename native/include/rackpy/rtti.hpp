// SPDX-License-Identifier: GPL-3.0-or-later
// rttiCast: dynamic_cast that also works across images whose copies of a type's type_info differ.
//
// A class without a key function (every virtual inline: rack::app::MultiLightWidget, TransparentWidget and
// many more) gets its type_info emitted, hidden, in each image that uses it: libRack has one copy and
// rackpy_host.so another. An object whose hierarchy passes through a type_info defined in libRack (say a
// ModuleLightWidget) then names libRack's copy as its base, and dynamic_cast<MultiLightWidget*> in the host
// compares it with the host's copy. libstdc++ compares type_infos by name, so Linux never sees a difference;
// libc++abi (macOS) compares them by address, so the cast fails there (RP-40). Visibility on RackPy's side
// cannot help: libRack's own copy is not exported.
//
// rttiCast tries dynamic_cast first; only where that fails, and only off libstdc++, it walks the object's
// dynamic type through the Itanium C++ ABI's class type_info records (section 2.9.5), comparing names as
// libstdc++ does, along public non-virtual bases, and refuses an ambiguous base as dynamic_cast does. libc++abi
// does not publish those record types in a header, so their ABI-specified layouts are mirrored here.
#pragma once
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <typeinfo>

namespace rackpy {
namespace rtti {

// The Itanium C++ ABI's type_info records for classes (2.9.5), as laid out by both libstdc++ and libc++abi.
struct TypeInfoLayout {
	const void* vtable;
	const char* name;
};
struct SingleBaseLayout : TypeInfoLayout {
	const std::type_info* base;
};
struct BaseInfoLayout {
	const std::type_info* type;
	long offsetFlags;  // bit 0 virtual, bit 1 public, offset from bit 8
};
struct MultipleBasesLayout : TypeInfoLayout {
	unsigned int flags;
	unsigned int baseCount;
	BaseInfoLayout bases[1];
};
const long VIRTUAL_BASE = 0x1;
const long PUBLIC_BASE = 0x2;
const int OFFSET_SHIFT = 8;

inline bool sameType(const std::type_info& a, const std::type_info& b) {
	return &a == &b || a == b || std::strcmp(a.name(), b.name()) == 0;
}

// What kind of class type_info a record is, from its own dynamic type: __class_type_info (no bases),
// __si_class_type_info (one public non-virtual base at offset 0) or __vmi_class_type_info.
enum class Kind { None, Single, Multiple, Other };
inline Kind kindOf(const std::type_info& type) {
	const char* name = typeid(type).name();
	if (std::strcmp(name, "N10__cxxabiv117__class_type_infoE") == 0)
		return Kind::None;
	if (std::strcmp(name, "N10__cxxabiv120__si_class_type_infoE") == 0)
		return Kind::Single;
	if (std::strcmp(name, "N10__cxxabiv121__vmi_class_type_infoE") == 0)
		return Kind::Multiple;
	return Kind::Other;
}

// The offset of each public, non-virtually reached subobject of type `target` within an object of type
// `type`, counted into `found` (at most 2: more than one is ambiguous). Returns false if a record cannot be
// read (an unknown kind), in which case nothing may be concluded.
inline bool findBases(const std::type_info& type, const std::type_info& target, std::ptrdiff_t offset,
	std::ptrdiff_t* found, int& count) {
	if (sameType(type, target)) {
		if (count < 2)
			found[count] = offset;
		count++;
		return true;
	}
	switch (kindOf(type)) {
		case Kind::None:
			return true;
		case Kind::Single:
			return findBases(*reinterpret_cast<const SingleBaseLayout&>(type).base, target, offset, found, count);
		case Kind::Multiple: {
			const auto& info = reinterpret_cast<const MultipleBasesLayout&>(type);
			for (unsigned int i = 0; i < info.baseCount; i++) {
				const BaseInfoLayout& base = info.bases[i];
				if (!(base.offsetFlags & PUBLIC_BASE) || (base.offsetFlags & VIRTUAL_BASE))
					continue;  // not reachable by an implicit conversion, or at an offset known only at run time
				if (!findBases(*base.type, target, offset + (base.offsetFlags >> OFFSET_SHIFT), found, count))
					return false;
			}
			return true;
		}
		default:
			return false;
	}
}

// The offset of the one `target` subobject in an object of dynamic type `type`, or -1 if there is none, more
// than one, or the records cannot be read.
inline std::ptrdiff_t baseOffset(const std::type_info& type, const std::type_info& target) {
	std::ptrdiff_t found[2] = {-1, -1};
	int count = 0;
	if (!findBases(type, target, 0, found, count) || count != 1 || found[0] < 0)
		return -1;
	return found[0];
}

}  // namespace rtti

/** dynamic_cast<To>(p) for a pointer To; where that fails off libstdc++, the name-based fallback above. */
template<class To, class From, typename std::enable_if<std::is_pointer<To>::value, int>::type = 0>
To rttiCast(From* p) {
	if (!p)
		return nullptr;
	if (To result = dynamic_cast<To>(p))
		return result;
#if !defined(__GLIBCXX__)
	typedef typename std::remove_cv<typename std::remove_pointer<To>::type>::type Target;
	const std::ptrdiff_t offset = rtti::baseOffset(typeid(*p), typeid(Target));
	if (offset >= 0) {
		// The most derived object, by the vtable's offset-to-top, which does not depend on type_info identity.
		const volatile void* top = dynamic_cast<const volatile void*>(p);
		const char* whole = static_cast<const char*>(const_cast<const void*>(top));
		return reinterpret_cast<To>(const_cast<char*>(whole + offset));
	}
#endif
	return nullptr;
}

/** dynamic_cast<To>(r) for a reference To, with the same fallback; throws std::bad_cast as dynamic_cast does. */
template<class To, class From, typename std::enable_if<std::is_reference<To>::value, int>::type = 0>
To rttiCast(From& r) {
	auto* p = rttiCast<typename std::remove_reference<To>::type*>(&r);
	if (!p)
		throw std::bad_cast();
	return *p;
}

}  // namespace rackpy
