// Copyright 2018 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Scripting/Python/Modules/controllermodule.h"

#include <optional>

#include "Core/API/Controller.h"
#include "Core/Config/MainSettings.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"
#include "Core/IOS/USB/Emulated/WUP028.h"
#include "Scripting/Python/PyScriptingBackend.h"
#include "Scripting/Python/Utils/module.h"

namespace PyScripting
{
constexpr float pi = 3.141592654f;

struct ControllerModuleState
{
  API::BaseManip* gc_manip;
  API::BaseManip* wii_manip;
  API::BaseManip* wii_classic_manip;
  API::BaseManip* wii_nunchuk_manip;
  API::BaseManip* gba_manip;
};

static PyObject* get_gc_buttons(PyObject* module, PyObject* args)
{
  const auto controller_id_opt = Py::ParseTuple<int>(args);
  if (!controller_id_opt.has_value())
    return nullptr;
  const int controller_id = std::get<0>(controller_id_opt.value());
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  const auto get_bool = [&](const API::InputKey& input_key) {
    return state->gc_manip->Get(controller_id, input_key) != 0 ? Py_True : Py_False;
  };
  const auto get_analog = [&](const API::InputKey& input_key) {
    return state->gc_manip->Get(controller_id, input_key);
  };
  return Py_BuildValue("{s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O,"
                       "s:d,s:d,s:d,s:d,s:d,s:d}",
      "A", get_bool(API::InputKey::GC_A),
      "B", get_bool(API::InputKey::GC_B),
      "X", get_bool(API::InputKey::GC_X),
      "Y", get_bool(API::InputKey::GC_Y),
      "Z", get_bool(API::InputKey::GC_Z),
      "Start", get_bool(API::InputKey::GC_START),
      "Up", get_bool(API::InputKey::GC_UP),
      "Down", get_bool(API::InputKey::GC_DOWN),
      "Left", get_bool(API::InputKey::GC_LEFT),
      "Right", get_bool(API::InputKey::GC_RIGHT),
      "L", get_bool(API::InputKey::GC_L),
      "R", get_bool(API::InputKey::GC_R),

      "StickX", get_analog(API::InputKey::GC_STICK_X),
      "StickY", get_analog(API::InputKey::GC_STICK_Y),
      "CStickX", get_analog(API::InputKey::GC_C_STICK_X),
      "CStickY", get_analog(API::InputKey::GC_C_STICK_Y),
      "TriggerLeft", get_analog(API::InputKey::GC_L_ANALOG),
      "TriggerRight", get_analog(API::InputKey::GC_R_ANALOG)
  );
}

static PyObject* set_gc_buttons(PyObject* module, PyObject* args)
{
  int controller_id;
  PyObject* dict;
  if (!PyArg_ParseTuple(args, "iO!", &controller_id, &PyDict_Type, &dict))
    return nullptr;
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);

  constexpr auto clear_on = API::ClearOn::NextFrame;
  const auto set_bool = [&](const API::InputKey& input_key, PyObject* py_object) {
    state->gc_manip->Set(controller_id, input_key, PyObject_IsTrue(py_object) ? 1 : 0, clear_on);
  };
  const auto set_analog = [&](const API::InputKey& input_key, PyObject* py_object) {
    state->gc_manip->Set(controller_id, input_key, PyFloat_AsDouble(py_object), clear_on);
  };

  PyObject* py_button_a = PyDict_GetItemString(dict, "A");
  PyObject* py_button_b = PyDict_GetItemString(dict, "B");
  PyObject* py_button_x = PyDict_GetItemString(dict, "X");
  PyObject* py_button_y = PyDict_GetItemString(dict, "Y");
  PyObject* py_button_z = PyDict_GetItemString(dict, "Z");
  PyObject* py_button_start = PyDict_GetItemString(dict, "Start");
  PyObject* py_button_up = PyDict_GetItemString(dict, "Up");
  PyObject* py_button_down = PyDict_GetItemString(dict, "Down");
  PyObject* py_button_left = PyDict_GetItemString(dict, "Left");
  PyObject* py_button_right = PyDict_GetItemString(dict, "Right");
  PyObject* py_button_l = PyDict_GetItemString(dict, "L");
  PyObject* py_button_r = PyDict_GetItemString(dict, "R");
  if (py_button_a != nullptr) set_bool(API::InputKey::GC_A, py_button_a);
  if (py_button_b != nullptr) set_bool(API::InputKey::GC_B, py_button_b);
  if (py_button_x != nullptr) set_bool(API::InputKey::GC_X, py_button_x);
  if (py_button_y != nullptr) set_bool(API::InputKey::GC_Y, py_button_y);
  if (py_button_z != nullptr) set_bool(API::InputKey::GC_Z, py_button_z);
  if (py_button_start != nullptr) set_bool(API::InputKey::GC_START, py_button_start);
  if (py_button_up != nullptr) set_bool(API::InputKey::GC_UP, py_button_up);
  if (py_button_down != nullptr) set_bool(API::InputKey::GC_DOWN, py_button_down);
  if (py_button_left != nullptr) set_bool(API::InputKey::GC_LEFT, py_button_left);
  if (py_button_right != nullptr) set_bool(API::InputKey::GC_RIGHT, py_button_right);
  if (py_button_l != nullptr) set_bool(API::InputKey::GC_L, py_button_l);
  if (py_button_r != nullptr) set_bool(API::InputKey::GC_R, py_button_r);

  PyObject* py_stick_x = PyDict_GetItemString(dict, "StickX");
  PyObject* py_stick_y = PyDict_GetItemString(dict, "StickY");
  PyObject* py_c_stick_x = PyDict_GetItemString(dict, "CStickX");
  PyObject* py_c_stick_y = PyDict_GetItemString(dict, "CStickY");
  PyObject* py_trigger_left = PyDict_GetItemString(dict, "TriggerLeft");
  PyObject* py_trigger_right = PyDict_GetItemString(dict, "TriggerRight");
  if (py_stick_x != nullptr) set_analog(API::InputKey::GC_STICK_X, py_stick_x);
  if (py_stick_y != nullptr) set_analog(API::InputKey::GC_STICK_Y, py_stick_y);
  if (py_c_stick_x != nullptr) set_analog(API::InputKey::GC_C_STICK_X, py_c_stick_x);
  if (py_c_stick_y != nullptr) set_analog(API::InputKey::GC_C_STICK_Y, py_c_stick_y);
  if (py_trigger_left != nullptr) set_analog(API::InputKey::GC_L_ANALOG, py_trigger_left);
  if (py_trigger_right != nullptr) set_analog(API::InputKey::GC_R_ANALOG, py_trigger_right);

  Py_RETURN_NONE;
}

static PyObject* get_wiimote_buttons(PyObject* module, PyObject* args)
{
  const auto controller_id_opt = Py::ParseTuple<int>(args);
  if (!controller_id_opt.has_value())
    return nullptr;
  const int controller_id = std::get<0>(controller_id_opt.value());
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  const auto get_bool = [&](const API::InputKey& input_key) {
    return state->wii_manip->Get(controller_id, input_key) != 0 ? Py_True : Py_False;
  };
  return Py_BuildValue("{s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O}",
      "A", get_bool(API::InputKey::WII_A),
      "B", get_bool(API::InputKey::WII_B),
      "One", get_bool(API::InputKey::WII_ONE),
      "Two", get_bool(API::InputKey::WII_TWO),
      "Plus", get_bool(API::InputKey::WII_PLUS),
      "Minus", get_bool(API::InputKey::WII_MINUS),
      "Home", get_bool(API::InputKey::WII_HOME),
      "Up", get_bool(API::InputKey::WII_UP),
      "Down", get_bool(API::InputKey::WII_DOWN),
      "Left", get_bool(API::InputKey::WII_LEFT),
      "Right", get_bool(API::InputKey::WII_RIGHT)
  );
}

static PyObject* set_wiimote_buttons(PyObject* module, PyObject* args)
{
  int controller_id;
  PyObject* dict;
  if (!PyArg_ParseTuple(args, "iO!", &controller_id, &PyDict_Type, &dict))
    return nullptr;

  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);

  const auto set_bool = [&](const API::InputKey& input_key, PyObject* py_object) {
    state->wii_manip->Set(controller_id, input_key, PyObject_IsTrue(py_object) ? 1 : 0,
                                  API::ClearOn::NextFrame);
  };

  PyObject* py_a = PyDict_GetItemString(dict, "A");
  PyObject* py_b = PyDict_GetItemString(dict, "B");
  PyObject* py_one = PyDict_GetItemString(dict, "One");
  PyObject* py_two = PyDict_GetItemString(dict, "Two");
  PyObject* py_plus = PyDict_GetItemString(dict, "Plus");
  PyObject* py_minus = PyDict_GetItemString(dict, "Minus");
  PyObject* py_home = PyDict_GetItemString(dict, "Home");
  PyObject* py_up = PyDict_GetItemString(dict, "Up");
  PyObject* py_down = PyDict_GetItemString(dict, "Down");
  PyObject* py_left = PyDict_GetItemString(dict, "Left");
  PyObject* py_right = PyDict_GetItemString(dict, "Right");
  if (py_a != nullptr) set_bool(API::InputKey::WII_A, py_a);
  if (py_b != nullptr) set_bool(API::InputKey::WII_B, py_b);
  if (py_one != nullptr) set_bool(API::InputKey::WII_ONE, py_one);
  if (py_two != nullptr) set_bool(API::InputKey::WII_TWO, py_two);
  if (py_plus != nullptr) set_bool(API::InputKey::WII_PLUS, py_plus);
  if (py_minus != nullptr) set_bool(API::InputKey::WII_MINUS, py_minus);
  if (py_home != nullptr) set_bool(API::InputKey::WII_HOME, py_home);
  if (py_up != nullptr) set_bool(API::InputKey::WII_UP, py_up);
  if (py_down != nullptr) set_bool(API::InputKey::WII_DOWN, py_down);
  if (py_left != nullptr) set_bool(API::InputKey::WII_LEFT, py_left);
  if (py_right != nullptr) set_bool(API::InputKey::WII_RIGHT, py_right);

  Py_RETURN_NONE;
}

static PyObject* get_wiimote_pointer(PyObject* module, PyObject* args)
{
  const auto controller_id_opt = Py::ParseTuple<int>(args);
  if (!controller_id_opt.has_value())
    return nullptr;
  const int controller_id = std::get<0>(controller_id_opt.value());
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  return Py_BuildValue("(dd)", state->wii_manip->Get(controller_id, API::InputKey::WII_IR_X),
                       state->wii_manip->Get(controller_id, API::InputKey::WII_IR_Y));
}

static PyObject* set_wiimote_pointer(PyObject* module, PyObject* args)
{
  int controller_id;
  float x, y;
  if (!PyArg_ParseTuple(args, "iff", &controller_id, &x, &y))
    return nullptr;
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  state->wii_manip->Set(controller_id, API::InputKey::WII_IR_X, x, API::ClearOn::NextFrame);
  state->wii_manip->Set(controller_id, API::InputKey::WII_IR_Y, y, API::ClearOn::NextFrame);
  Py_RETURN_NONE;
}

static PyObject* get_wiimote_acceleration(PyObject* module, PyObject* args)
{
  const auto controller_id_opt = Py::ParseTuple<int>(args);
  if (!controller_id_opt.has_value())
    return nullptr;
  const int controller_id = std::get<0>(controller_id_opt.value());
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  return Py_BuildValue("(ddd)", state->wii_manip->Get(controller_id, API::InputKey::WII_ACCELERATION_X),
                       state->wii_manip->Get(controller_id, API::InputKey::WII_ACCELERATION_Y),
                       state->wii_manip->Get(controller_id, API::InputKey::WII_ACCELERATION_Z));
}

static PyObject* set_wiimote_acceleration(PyObject* module, PyObject* args)
{
  int controller_id;
  float x, y, z;
  if (!PyArg_ParseTuple(args, "ifff", &controller_id, &x, &y, &z))
    return nullptr;
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  state->wii_manip->Set(controller_id, API::InputKey::WII_ACCELERATION_X, x, API::ClearOn::NextFrame);
  state->wii_manip->Set(controller_id, API::InputKey::WII_ACCELERATION_Y, y, API::ClearOn::NextFrame);
  state->wii_manip->Set(controller_id, API::InputKey::WII_ACCELERATION_Z, z, API::ClearOn::NextFrame);
  Py_RETURN_NONE;
}

static PyObject* get_wiimote_angular_velocity(PyObject* module, PyObject* args)
{
  const auto controller_id_opt = Py::ParseTuple<int>(args);
  if (!controller_id_opt.has_value())
    return nullptr;
  const int controller_id = std::get<0>(controller_id_opt.value());
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  return Py_BuildValue("(ddd)",
                       state->wii_manip->Get(controller_id, API::InputKey::WII_ANGULAR_VELOCITY_X),
                       state->wii_manip->Get(controller_id, API::InputKey::WII_ANGULAR_VELOCITY_Y),
                       state->wii_manip->Get(controller_id, API::InputKey::WII_ANGULAR_VELOCITY_Z));
}

static PyObject* set_wiimote_angular_velocity(PyObject* module, PyObject* args)
{
  int controller_id;
  float x, y, z;
  if (!PyArg_ParseTuple(args, "ifff", &controller_id, &x, &y, &z))
    return nullptr;
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  state->wii_manip->Set(controller_id, API::InputKey::WII_ANGULAR_VELOCITY_X, x, API::ClearOn::NextFrame);
  state->wii_manip->Set(controller_id, API::InputKey::WII_ANGULAR_VELOCITY_Y, y, API::ClearOn::NextFrame);
  state->wii_manip->Set(controller_id, API::InputKey::WII_ANGULAR_VELOCITY_Z, z, API::ClearOn::NextFrame);
  Py_RETURN_NONE;
}

static PyObject* get_wii_classic_buttons(PyObject* module, PyObject* args)
{
  const auto controller_id_opt = Py::ParseTuple<int>(args);
  if (!controller_id_opt.has_value())
    return nullptr;
  const int controller_id = std::get<0>(controller_id_opt.value());
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  const auto get_bool = [&](const API::InputKey& input_key) {
    return state->wii_classic_manip->Get(controller_id, input_key) != 0 ? Py_True : Py_False;
  };
  const auto get_analog = [&](const API::InputKey& input_key) {
    return state->wii_classic_manip->Get(controller_id, input_key);
  };

  return Py_BuildValue("{s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O,"
                       "s:d,s:d,s:d,s:d,s:d,s:d}",
    "A", get_bool(API::InputKey::WII_CLASSIC_A),
    "B", get_bool(API::InputKey::WII_CLASSIC_B),
    "X", get_bool(API::InputKey::WII_CLASSIC_X),
    "Y", get_bool(API::InputKey::WII_CLASSIC_Y),
    "ZL", get_bool(API::InputKey::WII_CLASSIC_ZL),
    "ZR", get_bool(API::InputKey::WII_CLASSIC_ZR),
    "Plus", get_bool(API::InputKey::WII_CLASSIC_PLUS),
    "Minus", get_bool(API::InputKey::WII_CLASSIC_MINUS),
    "Home", get_bool(API::InputKey::WII_CLASSIC_HOME),
    "Up", get_bool(API::InputKey::WII_CLASSIC_UP),
    "Down", get_bool(API::InputKey::WII_CLASSIC_DOWN),
    "Left", get_bool(API::InputKey::WII_CLASSIC_LEFT),
    "Right", get_bool(API::InputKey::WII_CLASSIC_RIGHT),
    "L", get_bool(API::InputKey::WII_CLASSIC_L),
    "R", get_bool(API::InputKey::WII_CLASSIC_R),
    "TriggerLeft", get_analog(API::InputKey::WII_CLASSIC_L_ANALOG),
    "TriggerRight", get_analog(API::InputKey::WII_CLASSIC_R_ANALOG),
    "LeftStickX", get_analog(API::InputKey::WII_CLASSIC_LEFT_STICK_X),
    "LeftStickY", get_analog(API::InputKey::WII_CLASSIC_LEFT_STICK_Y),
    "RightStickX", get_analog(API::InputKey::WII_CLASSIC_RIGHT_STICK_X),
    "RightStickY", get_analog(API::InputKey::WII_CLASSIC_RIGHT_STICK_Y)
  );
}

static PyObject* set_wii_classic_buttons(PyObject* module, PyObject* args)
{
  int controller_id;
  PyObject* dict;
  if (!PyArg_ParseTuple(args, "iO!", &controller_id, &PyDict_Type, &dict))
    return nullptr;

  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);

  const auto set_bool = [&](const API::InputKey& input_key, PyObject* py_object) {
    state->wii_classic_manip->Set(controller_id, input_key, PyObject_IsTrue(py_object) ? 1 : 0,
                                  API::ClearOn::NextFrame);
  };
  const auto set_analog = [&](const API::InputKey& input_key, PyObject* py_object) {
    state->wii_classic_manip->Set(controller_id, input_key, PyFloat_AsDouble(py_object),
                                  API::ClearOn::NextFrame);
  };

  PyObject* py_a = PyDict_GetItemString(dict, "A");
  PyObject* py_b = PyDict_GetItemString(dict, "B");
  PyObject* py_x = PyDict_GetItemString(dict, "X");
  PyObject* py_y = PyDict_GetItemString(dict, "Y");
  PyObject* py_zl = PyDict_GetItemString(dict, "ZL");
  PyObject* py_zr = PyDict_GetItemString(dict, "ZR");
  PyObject* py_plus = PyDict_GetItemString(dict, "Plus");
  PyObject* py_minus = PyDict_GetItemString(dict, "Minus");
  PyObject* py_home = PyDict_GetItemString(dict, "Home");
  PyObject* py_up = PyDict_GetItemString(dict, "Up");
  PyObject* py_down = PyDict_GetItemString(dict, "Down");
  PyObject* py_left = PyDict_GetItemString(dict, "Left");
  PyObject* py_right = PyDict_GetItemString(dict, "Right");
  PyObject* py_l = PyDict_GetItemString(dict, "L");
  PyObject* py_r = PyDict_GetItemString(dict, "R");
  PyObject* py_trigger_l = PyDict_GetItemString(dict, "TriggerLeft");
  PyObject* py_trigger_r = PyDict_GetItemString(dict, "TriggerRight");
  PyObject* py_left_stick_x = PyDict_GetItemString(dict, "LeftStickX");
  PyObject* py_left_stick_y = PyDict_GetItemString(dict, "LeftStickY");
  PyObject* py_right_stick_x = PyDict_GetItemString(dict, "RightStickX");
  PyObject* py_right_stick_y = PyDict_GetItemString(dict, "RightStickY");
  if (py_a != nullptr)
    set_bool(API::InputKey::WII_CLASSIC_A, py_a);
  if (py_b != nullptr)
    set_bool(API::InputKey::WII_CLASSIC_B, py_b);
  if (py_x != nullptr)
    set_bool(API::InputKey::WII_CLASSIC_X, py_x);
  if (py_y != nullptr)
    set_bool(API::InputKey::WII_CLASSIC_Y, py_y);
  if (py_zl != nullptr)
    set_bool(API::InputKey::WII_CLASSIC_ZL, py_zl);
  if (py_zr != nullptr)
    set_bool(API::InputKey::WII_CLASSIC_ZR, py_zr);
  if (py_plus != nullptr)
    set_bool(API::InputKey::WII_CLASSIC_PLUS, py_plus);
  if (py_minus != nullptr)
    set_bool(API::InputKey::WII_CLASSIC_MINUS, py_minus);
  if (py_home != nullptr)
    set_bool(API::InputKey::WII_CLASSIC_HOME, py_home);
  if (py_up != nullptr)
    set_bool(API::InputKey::WII_CLASSIC_UP, py_up);
  if (py_down != nullptr)
    set_bool(API::InputKey::WII_CLASSIC_DOWN, py_down);
  if (py_left != nullptr)
    set_bool(API::InputKey::WII_CLASSIC_LEFT, py_left);
  if (py_right != nullptr)
    set_bool(API::InputKey::WII_CLASSIC_RIGHT, py_right);
  if (py_l != nullptr)
    set_bool(API::InputKey::WII_CLASSIC_L, py_l);
  if (py_r != nullptr)
    set_bool(API::InputKey::WII_CLASSIC_R, py_r);
  if (py_trigger_l != nullptr)
    set_analog(API::InputKey::WII_CLASSIC_L_ANALOG, py_trigger_l);
  if (py_trigger_r != nullptr)
    set_analog(API::InputKey::WII_CLASSIC_R_ANALOG, py_trigger_r);
  if (py_left_stick_x != nullptr)
    set_analog(API::InputKey::WII_CLASSIC_LEFT_STICK_X, py_left_stick_x);
  if (py_left_stick_y != nullptr)
    set_analog(API::InputKey::WII_CLASSIC_LEFT_STICK_Y, py_left_stick_y);
  if (py_right_stick_x != nullptr)
    set_analog(API::InputKey::WII_CLASSIC_RIGHT_STICK_X, py_right_stick_x);
  if (py_right_stick_y != nullptr)
    set_analog(API::InputKey::WII_CLASSIC_RIGHT_STICK_Y, py_right_stick_y);

  Py_RETURN_NONE;
}

static PyObject* get_wii_nunchuk_buttons(PyObject* module, PyObject* args)
{
  const auto controller_id_opt = Py::ParseTuple<int>(args);
  if (!controller_id_opt.has_value())
    return nullptr;
  const int controller_id = std::get<0>(controller_id_opt.value());
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  const auto get_bool = [&](const API::InputKey& input_key) {
    return state->wii_nunchuk_manip->Get(controller_id, input_key) != 0 ? Py_True : Py_False;
  };
  const auto get_analog = [&](const API::InputKey& input_key) {
    return state->wii_nunchuk_manip->Get(controller_id, input_key);
  };

  return Py_BuildValue("{s:O,s:O,s:d,s:d}",
    "C", get_bool(API::InputKey::WII_NUNCHUK_C),
    "Z", get_bool(API::InputKey::WII_NUNCHUK_Z),
    "StickX", get_analog(API::InputKey::WII_NUNCHUK_STICK_X),
    "StickY", get_analog(API::InputKey::WII_NUNCHUK_STICK_Y)
  );
}

static PyObject* set_wii_nunchuk_buttons(PyObject* module, PyObject* args)
{
  int controller_id;
  PyObject* dict;
  if (!PyArg_ParseTuple(args, "iO!", &controller_id, &PyDict_Type, &dict))
    return nullptr;

  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);

  const auto set_bool = [&](const API::InputKey& input_key, PyObject* py_object) {
    state->wii_nunchuk_manip->Set(controller_id, input_key, PyObject_IsTrue(py_object) ? 1 : 0,
                                  API::ClearOn::NextFrame);
  };
  const auto set_analog = [&](const API::InputKey& input_key, PyObject* py_object) {
    state->wii_nunchuk_manip->Set(controller_id, input_key, PyFloat_AsDouble(py_object),
                                  API::ClearOn::NextFrame);
  };

  PyObject* py_c = PyDict_GetItemString(dict, "C");
  PyObject* py_z = PyDict_GetItemString(dict, "Z");
  PyObject* py_stick_x = PyDict_GetItemString(dict, "StickX");
  PyObject* py_stick_y = PyDict_GetItemString(dict, "StickY");
  if (py_c != nullptr)
    set_bool(API::InputKey::WII_NUNCHUK_C, py_c);
  if (py_z != nullptr)
    set_bool(API::InputKey::WII_NUNCHUK_Z, py_z);
  if (py_stick_x != nullptr)
    set_analog(API::InputKey::WII_NUNCHUK_STICK_X, py_stick_x);
  if (py_stick_y != nullptr)
    set_analog(API::InputKey::WII_NUNCHUK_STICK_Y, py_stick_y);

  Py_RETURN_NONE;
}

static PyObject* get_wii_nunchuk_acceleration(PyObject* module, PyObject* args)
{
  const auto controller_id_opt = Py::ParseTuple<int>(args);
  if (!controller_id_opt.has_value())
    return nullptr;
  const int controller_id = std::get<0>(controller_id_opt.value());
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  return Py_BuildValue(
      "(ddd)",
      state->wii_nunchuk_manip->Get(controller_id, API::InputKey::WII_NUNCHUCK_ACCELERATION_X),
      state->wii_nunchuk_manip->Get(controller_id, API::InputKey::WII_NUNCHUCK_ACCELERATION_Y),
      state->wii_nunchuk_manip->Get(controller_id, API::InputKey::WII_NUNCHUCK_ACCELERATION_Z));
}

static PyObject* set_wii_nunchuk_acceleration(PyObject* module, PyObject* args)
{
  int controller_id;
  float x, y, z;
  if (!PyArg_ParseTuple(args, "ifff", &controller_id, &x, &y, &z))
    return nullptr;
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  state->wii_nunchuk_manip->Set(controller_id, API::InputKey::WII_NUNCHUCK_ACCELERATION_X, x,
                                API::ClearOn::NextFrame);
  state->wii_nunchuk_manip->Set(controller_id, API::InputKey::WII_NUNCHUCK_ACCELERATION_Y, y,
                                API::ClearOn::NextFrame);
  state->wii_nunchuk_manip->Set(controller_id, API::InputKey::WII_NUNCHUCK_ACCELERATION_Z, z,
                                API::ClearOn::NextFrame);
  Py_RETURN_NONE;
}

// The emulated Wii U GameCube Controller Adapter (Core/IOS/USB/Emulated/WUP028.h). Its ports
// are fed from the GameCube controllers, so set_gc_buttons drives them too. These only plug it
// in and out, change what's in its ports, and read back what the Wii side did with it.
static PyObject* get_wup028(PyObject* module, PyObject* args)
{
  const IOS::HLE::USB::WUP028Stats stats = IOS::HLE::USB::GetWUP028Stats();
  const auto py_bool = [](bool b) { return b ? Py_True : Py_False; };
  return Py_BuildValue("{s:O,s:O,s:O,s:i,s:i,s:O,s:i,s:(iiii),s:(OOOO),s:I,s:I,s:I}",
      "plugged", Config::Get(Config::MAIN_EMULATE_WUP028) ? Py_True : Py_False,
      "attached", stats.attached ? Py_True : Py_False,
      "started", stats.started ? Py_True : Py_False,
      "ports", Config::Get(Config::MAIN_EMULATE_WUP028_PORTS),
      "wireless", Config::Get(Config::MAIN_EMULATE_WUP028_WIRELESS),
      "rumble_power", Config::Get(Config::MAIN_EMULATE_WUP028_RUMBLE_POWER) ? Py_True : Py_False,
      "short_report_every", Config::Get(Config::MAIN_EMULATE_WUP028_SHORT_REPORT_EVERY),
      "rumble", stats.rumble[0], stats.rumble[1], stats.rumble[2], stats.rumble[3],
      "motors", py_bool(stats.motors[0]), py_bool(stats.motors[1]), py_bool(stats.motors[2]),
      py_bool(stats.motors[3]),
      "reports", stats.reports,
      "short_reports", stats.short_reports,
      "rumble_messages", stats.rumble_messages);
}

static PyObject* set_wup028(PyObject* module, PyObject* args)
{
  PyObject* dict;
  if (!PyArg_ParseTuple(args, "O!", &PyDict_Type, &dict))
    return nullptr;

  // Every value is read and checked first, so a bad one changes nothing.
  const auto read_bool = [&](const char* key, std::optional<bool>& out) {
    PyObject* v = PyDict_GetItemString(dict, key);
    if (v == nullptr)
      return true;
    const int truth = PyObject_IsTrue(v);
    if (truth < 0)
      return false;
    out = truth == 1;
    return true;
  };
  const auto read_int = [&](const char* key, long min, long max, std::optional<int>& out) {
    PyObject* v = PyDict_GetItemString(dict, key);
    if (v == nullptr)
      return true;
    if (!PyLong_Check(v))
    {
      PyErr_Format(PyExc_TypeError, "%s must be an int", key);
      return false;
    }
    const long n = PyLong_AsLong(v);
    if (PyErr_Occurred())
      return false;
    if (n < min || n > max)
    {
      PyErr_Format(PyExc_ValueError, "%s must be %ld to %ld", key, min, max);
      return false;
    }
    out = static_cast<int>(n);
    return true;
  };
  std::optional<bool> plugged, rumble_power;
  std::optional<int> ports, wireless, short_report_every;
  if (!read_bool("plugged", plugged) || !read_int("ports", 0, 0xF, ports) ||
      !read_int("wireless", 0, 0xF, wireless) || !read_bool("rumble_power", rumble_power) ||
      !read_int("short_report_every", 0, 1000000, short_report_every))
  {
    return nullptr;
  }

  // Plugging in and out takes effect on the next USB scan (every 50 ms).
  if (plugged)
    Config::SetCurrent(Config::MAIN_EMULATE_WUP028, *plugged);
  if (ports)
    Config::SetCurrent(Config::MAIN_EMULATE_WUP028_PORTS, *ports);
  if (wireless)
    Config::SetCurrent(Config::MAIN_EMULATE_WUP028_WIRELESS, *wireless);
  if (rumble_power)
    Config::SetCurrent(Config::MAIN_EMULATE_WUP028_RUMBLE_POWER, *rumble_power);
  if (short_report_every)
    Config::SetCurrent(Config::MAIN_EMULATE_WUP028_SHORT_REPORT_EVERY, *short_report_every);
  Py_RETURN_NONE;
}

static PyObject* get_gba_buttons(PyObject* module, PyObject* args)
{
  const auto controller_id_opt = Py::ParseTuple<int>(args);
  if (!controller_id_opt.has_value())
    return nullptr;
  const int controller_id = std::get<0>(controller_id_opt.value());
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  const auto get_bool = [&](const API::InputKey& input_key) {
    return state->gba_manip->Get(controller_id, input_key) != 0 ? Py_True : Py_False;
  };
  return Py_BuildValue("{s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O,s:O}",
    "A", get_bool(API::InputKey::GBA_A),
    "B", get_bool(API::InputKey::GBA_B),
    "L", get_bool(API::InputKey::GBA_L),
    "R", get_bool(API::InputKey::GBA_R),
    "Start", get_bool(API::InputKey::GBA_START),
    "Select", get_bool(API::InputKey::GBA_SELECT),
    "Up", get_bool(API::InputKey::GBA_UP),
    "Down", get_bool(API::InputKey::GBA_DOWN),
    "Left", get_bool(API::InputKey::GBA_LEFT),
    "Right", get_bool(API::InputKey::GBA_RIGHT));
}

static PyObject* set_gba_buttons(PyObject* module, PyObject* args)
{
  int controller_id;
  PyObject* dict;
  if (!PyArg_ParseTuple(args, "iO!", &controller_id, &PyDict_Type, &dict))
    return nullptr;
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);

  constexpr auto clear_on = API::ClearOn::NextFrame;
  const auto set_bool = [&](const API::InputKey& input_key, PyObject* py_object) {
    state->gba_manip->Set(controller_id, input_key, PyObject_IsTrue(py_object) ? 1 : 0, clear_on);
  };

  PyObject* py_button_a = PyDict_GetItemString(dict, "A");
  PyObject* py_button_b = PyDict_GetItemString(dict, "B");
  PyObject* py_button_l = PyDict_GetItemString(dict, "L");
  PyObject* py_button_r = PyDict_GetItemString(dict, "R");
  PyObject* py_button_start = PyDict_GetItemString(dict, "Start");
  PyObject* py_button_select = PyDict_GetItemString(dict, "Select");
  PyObject* py_button_up = PyDict_GetItemString(dict, "Up");
  PyObject* py_button_down = PyDict_GetItemString(dict, "Down");
  PyObject* py_button_left = PyDict_GetItemString(dict, "Left");
  PyObject* py_button_right = PyDict_GetItemString(dict, "Right");
  if (py_button_a != nullptr)
    set_bool(API::InputKey::GBA_A, py_button_a);
  if (py_button_b != nullptr)
    set_bool(API::InputKey::GBA_B, py_button_b);
  if (py_button_l != nullptr)
    set_bool(API::InputKey::GBA_L, py_button_l);
  if (py_button_r != nullptr)
    set_bool(API::InputKey::GBA_R, py_button_r);
  if (py_button_start != nullptr)
    set_bool(API::InputKey::GBA_START, py_button_start);
  if (py_button_select != nullptr)
    set_bool(API::InputKey::GBA_START, py_button_select);
  if (py_button_up != nullptr)
    set_bool(API::InputKey::GBA_UP, py_button_up);
  if (py_button_down != nullptr)
    set_bool(API::InputKey::GBA_DOWN, py_button_down);
  if (py_button_left != nullptr)
    set_bool(API::InputKey::GBA_LEFT, py_button_left);
  if (py_button_right != nullptr)
    set_bool(API::InputKey::GBA_RIGHT, py_button_right);

  Py_RETURN_NONE;
}

static PyObject* get_wiimote_swing(PyObject* module, PyObject* args)
{
  const auto controller_id_opt = Py::ParseTuple<int>(args);
  if (!controller_id_opt.has_value())
    return nullptr;
  const int controller_id = std::get<0>(controller_id_opt.value());
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  return Py_BuildValue("(ddddddd)",
                       state->wii_manip->Get(controller_id, API::InputKey::WII_SWING_X),
                       state->wii_manip->Get(controller_id, API::InputKey::WII_SWING_Y),
                       state->wii_manip->Get(controller_id, API::InputKey::WII_SWING_Z),
                       state->wii_manip->Get(controller_id, API::InputKey::WII_SWING_DISTANCE),
                       state->wii_manip->Get(controller_id, API::InputKey::WII_SWING_SPEED),
                       state->wii_manip->Get(controller_id, API::InputKey::WII_SWING_RETURN_SPEED),
                       state->wii_manip->Get(controller_id, API::InputKey::WII_SWING_ANGLE));
}

static PyObject* set_wiimote_swing(PyObject* module, PyObject* args)
{
  int controller_id;
  float x, y, z, distance, speed, return_speed, angle;
  if (!PyArg_ParseTuple(args, "ifffffff", &controller_id, &x, &y, &z, &distance, &speed, &return_speed, &angle))
    return nullptr;
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  state->wii_manip->Set(controller_id, API::InputKey::WII_SWING_X, x, API::ClearOn::NextFrame);
  state->wii_manip->Set(controller_id, API::InputKey::WII_SWING_Y, y, API::ClearOn::NextFrame);
  state->wii_manip->Set(controller_id, API::InputKey::WII_SWING_Z, z, API::ClearOn::NextFrame);
  state->wii_manip->Set(controller_id, API::InputKey::WII_SWING_DISTANCE, distance, API::ClearOn::NextFrame);
  state->wii_manip->Set(controller_id, API::InputKey::WII_SWING_SPEED, speed, API::ClearOn::NextFrame);
  state->wii_manip->Set(controller_id, API::InputKey::WII_SWING_RETURN_SPEED, return_speed, API::ClearOn::NextFrame);
  state->wii_manip->Set(controller_id, API::InputKey::WII_SWING_ANGLE, angle, API::ClearOn::NextFrame);
  Py_RETURN_NONE;
}

static PyObject* get_wiimote_shake(PyObject* module, PyObject* args)
{
  const auto controller_id_opt = Py::ParseTuple<int>(args);
  if (!controller_id_opt.has_value())
    return nullptr;
  const int controller_id = std::get<0>(controller_id_opt.value());
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  return Py_BuildValue("(ddddd)",
                       state->wii_manip->Get(controller_id, API::InputKey::WII_SHAKE_X),
                       state->wii_manip->Get(controller_id, API::InputKey::WII_SHAKE_Y),
                       state->wii_manip->Get(controller_id, API::InputKey::WII_SHAKE_Z),
                       state->wii_manip->Get(controller_id, API::InputKey::WII_SHAKE_INTENSITY),
                       state->wii_manip->Get(controller_id, API::InputKey::WII_SHAKE_FREQUENCY));
}

static PyObject* set_wiimote_shake(PyObject* module, PyObject* args)
{
  int controller_id;
  float x, y, z, intensity, frequency;
  if (!PyArg_ParseTuple(args, "ifffff", &controller_id, &x, &y, &z, &intensity, &frequency))
    return nullptr;
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  state->wii_manip->Set(controller_id, API::InputKey::WII_SHAKE_X, x, API::ClearOn::NextFrame);
  state->wii_manip->Set(controller_id, API::InputKey::WII_SHAKE_Y, y, API::ClearOn::NextFrame);
  state->wii_manip->Set(controller_id, API::InputKey::WII_SHAKE_Z, z, API::ClearOn::NextFrame);
  state->wii_manip->Set(controller_id, API::InputKey::WII_SHAKE_INTENSITY, intensity, API::ClearOn::NextFrame);
  state->wii_manip->Set(controller_id, API::InputKey::WII_SHAKE_FREQUENCY, frequency, API::ClearOn::NextFrame);
  Py_RETURN_NONE;
}

static PyObject* get_wiimote_tilt(PyObject* module, PyObject* args)
{
  const auto controller_id_opt = Py::ParseTuple<int>(args);
  if (!controller_id_opt.has_value())
    return nullptr;
  const int controller_id = std::get<0>(controller_id_opt.value());
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  return Py_BuildValue("(dddd)",
                       state->wii_manip->Get(controller_id, API::InputKey::WII_TILT_X),
                       state->wii_manip->Get(controller_id, API::InputKey::WII_TILT_Y),
                       state->wii_manip->Get(controller_id, API::InputKey::WII_TILT_ANGLE) * pi,
                       state->wii_manip->Get(controller_id, API::InputKey::WII_TILT_VELOCITY));
}

static PyObject* set_wiimote_tilt(PyObject* module, PyObject* args)
{
  int controller_id;
  float x, y, angle, velocity;
  if (!PyArg_ParseTuple(args, "iffff", &controller_id, &x, &y, &angle, &velocity))
    return nullptr;
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  state->wii_manip->Set(controller_id, API::InputKey::WII_TILT_X, x, API::ClearOn::NextFrame);
  state->wii_manip->Set(controller_id, API::InputKey::WII_TILT_Y, y, API::ClearOn::NextFrame);
  state->wii_manip->Set(controller_id, API::InputKey::WII_TILT_ANGLE, angle / pi, API::ClearOn::NextFrame);
  state->wii_manip->Set(controller_id, API::InputKey::WII_TILT_VELOCITY, velocity, API::ClearOn::NextFrame);
  Py_RETURN_NONE;
}

static PyObject* get_wii_nunchuk_swing(PyObject* module, PyObject* args)
{
  const auto controller_id_opt = Py::ParseTuple<int>(args);
  if (!controller_id_opt.has_value())
    return nullptr;
  const int controller_id = std::get<0>(controller_id_opt.value());
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  return Py_BuildValue("(ddddddd)",
                       state->wii_nunchuk_manip->Get(controller_id, API::InputKey::NUNCHUK_SWING_X),
                       state->wii_nunchuk_manip->Get(controller_id, API::InputKey::NUNCHUK_SWING_Y),
                       state->wii_nunchuk_manip->Get(controller_id, API::InputKey::NUNCHUK_SWING_Z),
                       state->wii_nunchuk_manip->Get(controller_id, API::InputKey::NUNCHUK_SWING_DISTANCE),
                       state->wii_nunchuk_manip->Get(controller_id, API::InputKey::NUNCHUK_SWING_SPEED),
                       state->wii_nunchuk_manip->Get(controller_id, API::InputKey::NUNCHUK_SWING_RETURN_SPEED),
                       state->wii_nunchuk_manip->Get(controller_id, API::InputKey::NUNCHUK_SWING_ANGLE));
}

static PyObject* set_wii_nunchuk_swing(PyObject* module, PyObject* args)
{
  int controller_id;
  float x, y, z, distance, speed, return_speed, angle;
  if (!PyArg_ParseTuple(args, "ifffffff", &controller_id, &x, &y, &z, &distance, &speed, &return_speed, &angle))
    return nullptr;
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  state->wii_nunchuk_manip->Set(controller_id, API::InputKey::NUNCHUK_SWING_X, x, API::ClearOn::NextFrame);
  state->wii_nunchuk_manip->Set(controller_id, API::InputKey::NUNCHUK_SWING_Y, y, API::ClearOn::NextFrame);
  state->wii_nunchuk_manip->Set(controller_id, API::InputKey::NUNCHUK_SWING_Z, z, API::ClearOn::NextFrame);
  state->wii_nunchuk_manip->Set(controller_id, API::InputKey::NUNCHUK_SWING_DISTANCE, distance, API::ClearOn::NextFrame);
  state->wii_nunchuk_manip->Set(controller_id, API::InputKey::NUNCHUK_SWING_SPEED, speed, API::ClearOn::NextFrame);
  state->wii_nunchuk_manip->Set(controller_id, API::InputKey::NUNCHUK_SWING_RETURN_SPEED, return_speed, API::ClearOn::NextFrame);
  state->wii_nunchuk_manip->Set(controller_id, API::InputKey::NUNCHUK_SWING_ANGLE, angle, API::ClearOn::NextFrame);
  Py_RETURN_NONE;
}

static PyObject* get_wii_nunchuk_shake(PyObject* module, PyObject* args)
{
  const auto controller_id_opt = Py::ParseTuple<int>(args);
  if (!controller_id_opt.has_value())
    return nullptr;
  const int controller_id = std::get<0>(controller_id_opt.value());
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  return Py_BuildValue("(ddddd)",
                       state->wii_nunchuk_manip->Get(controller_id, API::InputKey::NUNCHUK_SHAKE_X),
                       state->wii_nunchuk_manip->Get(controller_id, API::InputKey::NUNCHUK_SHAKE_Y),
                       state->wii_nunchuk_manip->Get(controller_id, API::InputKey::NUNCHUK_SHAKE_Z),
                       state->wii_nunchuk_manip->Get(controller_id, API::InputKey::NUNCHUK_SHAKE_INTENSITY),
                       state->wii_nunchuk_manip->Get(controller_id, API::InputKey::NUNCHUK_SHAKE_FREQUENCY));
}

static PyObject* set_wii_nunchuk_shake(PyObject* module, PyObject* args)
{
  int controller_id;
  float x, y, z, intensity, frequency;
  if (!PyArg_ParseTuple(args, "ifffff", &controller_id, &x, &y, &z, &intensity, &frequency))
    return nullptr;
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  state->wii_nunchuk_manip->Set(controller_id, API::InputKey::NUNCHUK_SHAKE_X, x, API::ClearOn::NextFrame);
  state->wii_nunchuk_manip->Set(controller_id, API::InputKey::NUNCHUK_SHAKE_Y, y, API::ClearOn::NextFrame);
  state->wii_nunchuk_manip->Set(controller_id, API::InputKey::NUNCHUK_SHAKE_Z, z, API::ClearOn::NextFrame);
  state->wii_nunchuk_manip->Set(controller_id, API::InputKey::NUNCHUK_SHAKE_INTENSITY, intensity, API::ClearOn::NextFrame);
  state->wii_nunchuk_manip->Set(controller_id, API::InputKey::NUNCHUK_SHAKE_FREQUENCY, frequency, API::ClearOn::NextFrame);
  Py_RETURN_NONE;
}

static PyObject* get_wii_nunchuk_tilt(PyObject* module, PyObject* args)
{
  const auto controller_id_opt = Py::ParseTuple<int>(args);
  if (!controller_id_opt.has_value())
    return nullptr;
  const int controller_id = std::get<0>(controller_id_opt.value());
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  return Py_BuildValue("(dddd)",
                       state->wii_nunchuk_manip->Get(controller_id, API::InputKey::NUNCHUK_TILT_X),
                       state->wii_nunchuk_manip->Get(controller_id, API::InputKey::NUNCHUK_TILT_Y),
                       state->wii_nunchuk_manip->Get(controller_id, API::InputKey::NUNCHUK_TILT_ANGLE) * pi,
                       state->wii_nunchuk_manip->Get(controller_id, API::InputKey::NUNCHUK_TILT_VELOCITY));
}

static PyObject* set_wii_nunchuk_tilt(PyObject* module, PyObject* args)
{
  int controller_id;
  float x, y, angle, velocity;
  if (!PyArg_ParseTuple(args, "iffff", &controller_id, &x, &y, &angle, &velocity))
    return nullptr;
  const ControllerModuleState* state = Py::GetState<ControllerModuleState>(module);
  state->wii_nunchuk_manip->Set(controller_id, API::InputKey::NUNCHUK_TILT_X, x, API::ClearOn::NextFrame);
  state->wii_nunchuk_manip->Set(controller_id, API::InputKey::NUNCHUK_TILT_Y, y, API::ClearOn::NextFrame);
  state->wii_nunchuk_manip->Set(controller_id, API::InputKey::NUNCHUK_TILT_ANGLE, angle / pi, API::ClearOn::NextFrame);
  state->wii_nunchuk_manip->Set(controller_id, API::InputKey::NUNCHUK_TILT_VELOCITY, velocity, API::ClearOn::NextFrame);
  Py_RETURN_NONE;
}

static void setup_controller_module(PyObject* module, ControllerModuleState* state)
{
  static const char pycode[] = R"(
# The typed dicts are also defined here make them available at runtime.
# They are copied from controller.pyi and should stay in sync with that file.
from typing import TypedDict


class GCInputs(TypedDict, total=False):
    """
    Dictionary describing the state of a GameCube controller.
    Boolean keys (buttons): True means pressed, False means released.
    Float keys for triggers: 0 means fully released, 1 means fully pressed.
    Float keys for sticks: 0 means neutral, ranges from -1 to 1.
    """
    A: bool
    B: bool
    X: bool
    Y: bool
    Z: bool
    Start: bool
    Up: bool
    Down: bool
    Left: bool
    Right: bool
    L: bool
    R: bool
    StickX: float
    StickY: float
    CStickX: float
    CStickY: float
    TriggerLeft: float
    TriggerRight: float


class WiimoteInputs(TypedDict, total=False):
    """
    Dictionary describing the state of a Wii Remote controller.
    Boolean keys (buttons): True means pressed, False means released.
    """
    A: bool
    B: bool
    One: bool
    Two: bool
    Plus: bool
    Minus: bool
    Home: bool
    Up: bool
    Down: bool
    Left: bool
    Right: bool


class WiiClassicInputs(TypedDict, total=False):
    """
    Dictionary describing the state of a Wii Classic controller.
    Boolean keys: True means pressed, False means released.
    Float keys for triggers: 0 means fully released, 1 means fully pressed.
    Float keys for sticks: 0 means neutral, ranges from -1 to 1.
    """
    A: bool
    B: bool
    X: bool
    Y: bool
    ZL: bool
    ZR: bool
    Plus: bool
    Minus: bool
    Home: bool
    Up: bool
    Down: bool
    Left: bool
    Right: bool
    L: bool
    R: bool
    TriggerLeft: float
    TriggerRight: float
    LeftStickX: float
    LeftStickY: float
    RightStickX: float
    RightStickY: float


class WiiNunchukInputs(TypedDict, total=False):
    """
    Dictionary describing the state of a Wii Nunchuk controller.
    Boolean keys (buttons): True means pressed, False means released.
    Float keys for sticks: 0 means neutral, ranges from -1 to 1.
    """
    C: bool
    Z: bool
    StickX: float
    StickY: float


class GBAInputs(TypedDict, total=False):
    """
    Dictionary describing the state of a GameBoy Advance controller.
    Boolean keys (buttons): True means pressed, False means released.
    """
    A: bool
    B: bool
    L: bool
    R: bool
    Start: bool
    Select: bool
    Up: bool
    Down: bool
    Left: bool
    Right: bool


class WUP028State(TypedDict, total=False):
    """
    The emulated Wii U GameCube Controller Adapter (get_wup028/set_wup028).
    Settable: plugged, ports, wireless, rumble_power, short_report_every.
    Read-only: attached, started, rumble, motors, reports, short_reports,
    rumble_messages.
    """
    plugged: bool
    ports: int
    wireless: int
    rumble_power: bool
    short_report_every: int
    attached: bool
    started: bool
    rumble: tuple[int, int, int, int]
    motors: tuple[bool, bool, bool, bool]
    reports: int
    short_reports: int
    rumble_messages: int

)";
  Py::Object result = Py::LoadPyCodeIntoModule(module, pycode);
  if (result.IsNull())
  {
    ERROR_LOG_FMT(SCRIPTING, "Failed to load embedded python code into controller module");
  }
  state->gc_manip = PyScriptingBackend::GetCurrent()->GetGCManip();
  state->wii_manip = PyScriptingBackend::GetCurrent()->GetWiiManip();
  state->wii_classic_manip = PyScriptingBackend::GetCurrent()->GetWiiClassicManip();
  state->wii_nunchuk_manip = PyScriptingBackend::GetCurrent()->GetWiiNunchukManip();
  state->gba_manip = PyScriptingBackend::GetCurrent()->GetGBAManip();
  PyScriptingBackend::GetCurrent()->AddCleanupFunc([state] {
    state->gc_manip->Clear();
    state->wii_manip->Clear();
    state->wii_classic_manip->Clear();
    state->wii_nunchuk_manip->Clear();
    state->gba_manip->Clear();
  });
}

PyMODINIT_FUNC PyInit_controller()
{
  static PyMethodDef method_defs[] = {
      {"get_gc_buttons", get_gc_buttons, METH_VARARGS, ""},
      {"set_gc_buttons", set_gc_buttons, METH_VARARGS, ""},
      {"get_wiimote_buttons", get_wiimote_buttons, METH_VARARGS, ""},
      {"set_wiimote_buttons", set_wiimote_buttons, METH_VARARGS, ""},
      {"get_wiimote_pointer", get_wiimote_pointer, METH_VARARGS, ""},
      {"set_wiimote_pointer", set_wiimote_pointer, METH_VARARGS, ""},
      {"get_wiimote_acceleration", get_wiimote_acceleration, METH_VARARGS, ""},
      {"set_wiimote_acceleration", set_wiimote_acceleration, METH_VARARGS, ""},
      {"get_wiimote_angular_velocity", get_wiimote_angular_velocity, METH_VARARGS, ""},
      {"set_wiimote_angular_velocity", set_wiimote_angular_velocity, METH_VARARGS, ""},
      {"get_wii_classic_buttons", get_wii_classic_buttons, METH_VARARGS, ""},
      {"set_wii_classic_buttons", set_wii_classic_buttons, METH_VARARGS, ""},
      {"get_wii_nunchuk_buttons", get_wii_nunchuk_buttons, METH_VARARGS, ""},
      {"set_wii_nunchuk_buttons", set_wii_nunchuk_buttons, METH_VARARGS, ""},
      {"get_wii_nunchuk_acceleration", get_wii_nunchuk_acceleration, METH_VARARGS, ""},
      {"set_wii_nunchuk_acceleration", set_wii_nunchuk_acceleration, METH_VARARGS, ""},
      {"get_wup028", get_wup028, METH_NOARGS, ""},
      {"set_wup028", set_wup028, METH_VARARGS, ""},
      {"get_gba_buttons", get_gba_buttons, METH_VARARGS, ""},
      {"set_gba_buttons", set_gba_buttons, METH_VARARGS, ""},
      {"get_wiimote_swing", get_wiimote_swing, METH_VARARGS, ""},
      {"set_wiimote_swing", set_wiimote_swing, METH_VARARGS, ""},
      {"get_wiimote_shake", get_wiimote_shake, METH_VARARGS, ""},
      {"set_wiimote_shake", set_wiimote_shake, METH_VARARGS, ""},
      {"get_wiimote_tilt", get_wiimote_tilt, METH_VARARGS, ""},
      {"set_wiimote_tilt", set_wiimote_tilt, METH_VARARGS, ""},
      {"get_wii_nunchuk_swing", get_wii_nunchuk_swing, METH_VARARGS, ""},
      {"set_wii_nunchuk_swing", set_wii_nunchuk_swing, METH_VARARGS, ""},
      {"get_wii_nunchuk_shake", get_wii_nunchuk_shake, METH_VARARGS, ""},
      {"set_wii_nunchuk_shake", set_wii_nunchuk_shake, METH_VARARGS, ""},
      {"get_wii_nunchuk_tilt", get_wii_nunchuk_tilt, METH_VARARGS, ""},
      {"set_wii_nunchuk_tilt", set_wii_nunchuk_tilt, METH_VARARGS, ""},
      {nullptr, nullptr, 0, nullptr}  // Sentinel
  };
  static PyModuleDef module_def =
      Py::MakeStatefulModuleDef<ControllerModuleState, setup_controller_module>("controller", method_defs);
  return PyModuleDef_Init(&module_def);
}

}  // namespace PyScripting
