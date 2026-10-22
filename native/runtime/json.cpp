// SPDX-License-Identifier: GPL-3.0-or-later
#include "rackpy/runtime/json.hpp"

#include <cmath>
#include <memory>
#include <string>

namespace rackpy {
namespace runtime {

namespace py = pybind11;

namespace {

struct JsonDeleter {
	void operator()(json_t* j) const { json_decref(j); }
};
using JsonPtr = std::unique_ptr<json_t, JsonDeleter>;

py::object fromJsonAt(const json_t* json, int depth) {
	if (depth > JSON_MAX_DEPTH)
		throw py::value_error("JSON nested deeper than " + std::to_string(JSON_MAX_DEPTH) + " levels");
	if (!json)
		return py::none();
	switch (json_typeof(json)) {
		case JSON_NULL: return py::none();
		case JSON_TRUE: return py::bool_(true);
		case JSON_FALSE: return py::bool_(false);
		case JSON_INTEGER: return py::int_((long long) json_integer_value(json));
		case JSON_REAL: return py::float_(json_real_value(json));
		case JSON_STRING: return py::str(json_string_value(json), json_string_length(json));
		case JSON_ARRAY: {
			py::list out;
			size_t index;
			json_t* item;
			json_array_foreach(const_cast<json_t*>(json), index, item) {
				out.append(fromJsonAt(item, depth + 1));
			}
			return out;
		}
		case JSON_OBJECT: {
			py::dict out;
			const char* key;
			json_t* item;
			json_object_foreach(const_cast<json_t*>(json), key, item) {
				out[py::str(key)] = fromJsonAt(item, depth + 1);
			}
			return out;
		}
	}
	throw py::value_error("unknown JSON type");
}

json_t* checked(json_t* j, const char* what) {
	if (!j)
		throw py::value_error(std::string("jansson could not represent ") + what);
	return j;
}

json_t* toJsonAt(py::handle value, int depth) {
	if (depth > JSON_MAX_DEPTH)
		throw py::value_error("value nested deeper than " + std::to_string(JSON_MAX_DEPTH) +
			" levels (or containing itself) cannot become JSON");
	if (value.is_none())
		return json_null();
	if (PyBool_Check(value.ptr()))
		return json_boolean(value.ptr() == Py_True);
	if (PyLong_Check(value.ptr())) {
		int overflow = 0;
		long long v = PyLong_AsLongLongAndOverflow(value.ptr(), &overflow);
		if (overflow) {
			PyErr_SetString(PyExc_OverflowError, "int does not fit a JSON integer (json_int_t, 64-bit)");
			throw py::error_already_set();
		}
		return checked(json_integer((json_int_t) v), "this integer");
	}
	if (PyFloat_Check(value.ptr())) {
		double v = PyFloat_AsDouble(value.ptr());
		if (!std::isfinite(v))
			throw py::value_error("JSON cannot represent NaN or infinity");
		return checked(json_real(v), "this float");
	}
	if (PyUnicode_Check(value.ptr())) {
		Py_ssize_t size = 0;
		const char* data = PyUnicode_AsUTF8AndSize(value.ptr(), &size);
		if (!data)
			throw py::error_already_set();
		return checked(json_stringn(data, (size_t) size), "this string");
	}
	if (PyList_Check(value.ptr()) || PyTuple_Check(value.ptr())) {
		JsonPtr array(json_array());
		for (py::handle item : value)
			json_array_append_new(array.get(), toJsonAt(item, depth + 1));
		return array.release();
	}
	if (PyDict_Check(value.ptr())) {
		JsonPtr object(json_object());
		for (auto item : py::reinterpret_borrow<py::dict>(value)) {
			if (!PyUnicode_Check(item.first.ptr()))
				throw py::type_error("JSON object keys must be str, not " + std::string(Py_TYPE(item.first.ptr())->tp_name));
			std::string key = item.first.cast<std::string>();
			if (key.find('\0') != std::string::npos)
				throw py::value_error("JSON object keys cannot contain NUL characters");
			json_object_set_new(object.get(), key.c_str(), toJsonAt(item.second, depth + 1));
		}
		return object.release();
	}
	throw py::type_error(std::string("a ") + Py_TYPE(value.ptr())->tp_name +
		" cannot become JSON; use None, bool, int, float, str, list, tuple or dict");
}

}  // namespace

py::object fromJson(const json_t* json) {
	return fromJsonAt(json, 0);
}

json_t* toJson(py::handle value) {
	return toJsonAt(value, 0);
}

}  // namespace runtime
}  // namespace rackpy
