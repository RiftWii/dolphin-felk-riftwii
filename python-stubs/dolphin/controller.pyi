"""
Module for programmatic inputs.
"""
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


def get_gc_buttons(controller_id: int, /) -> GCInputs:
    """
    Retrieves the current input map for the given GameCube controller.
    All keys of :class:`GCInputs` will be present in the returned dictionary.
    :param controller_id: 0-based index of the controller
    :return: dictionary describing the current input map
    """


def set_gc_buttons(controller_id: int, inputs: GCInputs, /) -> None:
    """
    Sets the current input map for the given GameCube controller.

    All input keys omitted from the input map will not be applied
    and therefore stay in their current state.
    For example, you may only set the A button like this::

        controller.set_gc_buttons(0, {"A": True})

    The override will hold for the current frame.
    :param controller_id: 0-based index of the controller
    :param inputs: dictionary describing the input map
    """


def get_wiimote_buttons(controller_id: int, /) -> WiimoteInputs:
    """
    Retrieves the current input map for the given Wii remote.
    All keys of :class:`WiimoteInputs` will be present in the returned
    dictionary.
    :param controller_id: 0-based index of the controller
    :return: dictionary describing the current input map
    """


def set_wiimote_buttons(controller_id: int, inputs: WiimoteInputs,
                        /) -> None:
    """
    Sets the current input map for the given Wii remote.

    All input keys omitted from the input map will not be applied
    and therefore stay in their current state.
    For example, you may only set the A button like this::

        controller.set_wiimote_buttons(0, {"A": True})

    The override will hold for the current frame.
    :param controller_id: 0-based index of the controller
    :param inputs: dictionary describing the input map
    """


def get_wiimote_pointer(controller_id: int, /) -> (float, float):
    """
    Returns the current wii pointer position, in screen coordinates.
    For x, -1 and 1 represent the left and right edge of the screen.
    For y, -1 and 1 represent the bottom and top edge of the screen.
    The x and y values are allowed to exceed the [-1, 1] range.
    :param controller_id: 0-based index of the controller
    :return: The screen coordinates as a tuple (x, y)
    """


def set_wiimote_pointer(controller_id: int, x: float, y: float, /) -> None:
    """
    Sets the current wii pointer position, in screen coordinates.

    | For x, -1 and 1 represent the left and right edge of the screen.
    | For y, -1 and 1 represent the bottom and top edge of the screen.
    The x and y values are allowed to exceed the [-1, 1] range.
    The override will hold for the current frame.
    :param controller_id: 0-based index of the controller
    :param x: horizontal screen coordinate, where positive means right
    :param y: vertical screen coordinate, where positive means up
    """


def get_wiimote_acceleration(controller_id: int, /) -> (float, float, float):
    """
    Returns the current wii remote acceleration, as a vector (x, y, z).
    The acceleration is measured in m/s².
    :param controller_id: 0-based index of the controller
    :return: The current acceleration as a tuple (x, y, z)
    """


def set_wiimote_acceleration(controller_id: int,
                             x: float, y: float, z: float, /) -> None:
    """
    Sets the current wii remote acceleration, as a vector (x, y, z).
    The acceleration is measured in m/s².
    This function overrides set_wiimote_shake and set_wiimote_swing.
    The override will hold for the current frame.
    :param controller_id: 0-based index of the controller
    :param x: x-component of the acceleration vector
    :param y: y-component of the acceleration vector
    :param z: z-component of the acceleration vector
    """


def get_wiimote_angular_velocity(controller_id: int, /) \
        -> (float, float, float):
    """
    Returns the current wii remote angular velocity, as a vector (x, y, z).
    The angular velocity is measured in radians/s.
    :param controller_id: 0-based index of the controller
    :return: The current angular velocity as a tuple (x, y, z)
    """


def set_wiimote_angular_velocity(controller_id: int,
                                 x: float, y: float, z: float, /) -> None:
    """
    Sets the current wii remote angular velocity, as a vector (x, y, z).
    The angular velocity is measured in radians/s.
    This function overrides set_wiimote_tilt and set_wiimote_swing.
    The override will hold for the current frame.
    :param controller_id: 0-based index of the controller
    :param x: x-component of the angular velocity vector
    :param y: y-component of the angular velocity vector
    :param z: z-component of the angular velocity vector
    """


def get_wii_classic_buttons(controller_id: int, /) -> WiiClassicInputs:
    """
    Retrieves the current input map for the given Wii Classic controller.
    All keys of :class:`WiiClassicInputs` will be present in the returned
    dictionary.
    :param controller_id: 0-based index of the controller
    :return: dictionary describing the current input map
    """


def set_wii_classic_buttons(controller_id: int, inputs: WiiClassicInputs,
                            /) -> None:
    """
    Sets the current input map for the given Wii Classic controller.

    All input keys omitted from the input map will not be applied
    and therefore stay in their current state.
    For example, you may only set the A button like this::

        controller.set_wii_classic_buttons(0, {"A": True})

    The override will hold for the current frame.
    :param controller_id: 0-based index of the controller
    :param inputs: dictionary describing the input map
    """


def get_wiimote_swing(controller_id: int, /) \
        -> (float, float, float, float, float, float, float):
    """
    Returns the current wii remote swing state, as a vector (x, y, z, distance, speed, return_speed, angle).
    The angle is measured in radians. The distances are in meters.
    :param controller_id: 0-based index of the controller
    :return: The current swing state as a tuple (x, y, z, distance, speed, return_speed, angle)
    """


def set_wiimote_swing(controller_id: int, x: float, y: float, z: float,
                      distance: float, speed: float, return_speed: float, angle: float, /) -> None:
    """
    Sets the wii remote swing motion.
    This function is overridden by set_wiimote_acceleration and set_wiimote_angular_velocity.
    The override will hold for the current frame.
    :param controller_id: 0-based index of the controller
    :param x: x-component of the swing
    :param y: y-component of the swing
    :param z: z-component of the swing
    :param distance: distance of the swing (m)
    :param speed: speed of the swing (m/s)
    :param return_speed: return speed of the swing (m/s)
    :param angle: angle of the swing (rad)
    """


def get_wiimote_shake(controller_id: int, /) \
        -> (float, float, float, float, float):
    """
    Returns the current wii remote shake state, as a vector (x, y, z, intensity, frequency).
    The intensity is in meters, and the frequency in Hertz.
    :param controller_id: 0-based index of the controller
    :return: The current swing state as a tuple (x, y, z, intensity, frequency)
    """


def set_wiimote_shake(controller_id: int, x: float, y: float, z: float,
                      intensity: float, frequency: float, /) -> None:
    """
    Sets the wii remote shake motion.
    This function is overridden by set_wiimote_acceleration.
    The override will hold for the current frame.
    :param controller_id: 0-based index of the controller
    :param x: x-component of the shake
    :param y: y-component of the shake
    :param z: z-component of the shake
    :param intensity: intensity of the shake (m)
    :param frequency: frequency of the shake (Hz)
    """


def get_wiimote_tilt(controller_id: int, /) \
        -> (float, float, float, float):
    """
    Returns the current wii remote tilt motion state, as a vector (x, y, angle, velocity).
    The angle is in rad, and the velocity in rad/s
    :param controller_id: 0-based index of the controller
    :return: The current swing state as a tuple (x, y, angle, velocity)
    """


def set_wiimote_tilt(controller_id: int, x: float, y: float,
                     angle: float, velocity: float, /) -> None:
    """
    Sets the wii remote tilt motion.
    This function is overridden by set_wiimote_angular_velocity.
    The override will hold for the current frame.
    :param controller_id: 0-based index of the controller
    :param x: x-component of the tilt
    :param y: y-component of the tilt
    :param angle: max tilt angle (rad)
    :param velocity: rotational velocity of the tilt (rad/s)
    """


def get_wii_nunchuk_buttons(controller_id: int, /) -> WiiNunchukInputs:
    """
    Retrieves the current input map for the given Wii Nunchuk controller.
    All keys of :class:`WiiNunchukInputs` will be present in the returned
    dictionary.
    :param controller_id: 0-based index of the controller
    :return: dictionary describing the current input map
    """


def set_wii_nunchuk_buttons(controller_id: int, inputs: WiiNunchukInputs,
                            /) -> None:
    """
    Sets the current input map for the given Wii Nunchuk controller.

    All input keys omitted from the input map will not be applied
    and therefore stay in their current state.
    For example, you may only set the C button like this::

        controller.set_wii_nunchuk_buttons(0, {"C": True})

    The override will hold for the current frame.
    :param controller_id: 0-based index of the controller
    :param inputs: dictionary describing the input map
    """


def get_wii_nunchuk_acceleration(controller_id: int, /) \
        -> (float, float, float):
    """
    Returns the current wii nunchuk acceleration, as a vector (x, y, z).
    The acceleration is measured in m/s².
    :param controller_id: 0-based index of the controller
    :return: The current acceleration as a tuple (x, y, z)
    """


def set_wii_nunchuk_acceleration(controller_id: int,
                                 x: float, y: float, z: float, /) -> None:
    """
    Sets the current wii nunchuk acceleration, as a vector (x, y, z).
    The acceleration is measured in m/s².
    The override will hold for the current frame.
    :param controller_id: 0-based index of the controller
    :param x: x-component of the acceleration vector
    :param y: y-component of the acceleration vector
    :param z: z-component of the acceleration vector
    """


def get_wii_nunchuk_swing(controller_id: int, /) \
        -> (float, float, float, float, float, float, float):
    """
    Returns the current wii nunchuk swing state, as a vector (x, y, z, distance, speed, return_speed, angle).
    The angle is measured in radians. The distances are in meters.
    :param controller_id: 0-based index of the controller
    :return: The current swing state as a tuple (x, y, z, distance, speed, return_speed, angle)
    """


def set_wii_nunchuk_swing(controller_id: int, x: float, y: float, z: float,
                      distance: float, speed: float, return_speed: float, angle: float, /) -> None:
    """
    Sets the wii nunchuk swing motion.
    This function is overridden by set_wii_nunchuk_acceleration and set_wii_nunchuk_angular_velocity.
    The override will hold for the current frame.
    :param controller_id: 0-based index of the controller
    :param x: x-component of the swing
    :param y: y-component of the swing
    :param z: z-component of the swing
    :param distance: distance of the swing (m)
    :param speed: speed of the swing (m/s)
    :param return_speed: return speed of the swing (m/s)
    :param angle: angle of the swing (rad)
    """


def get_wii_nunchuk_shake(controller_id: int, /) \
        -> (float, float, float, float, float):
    """
    Returns the current wii nunchuk shake state, as a vector (x, y, z, intensity, frequency).
    The intensity is in meters, and the frequency in Hertz.
    :param controller_id: 0-based index of the controller
    :return: The current swing state as a tuple (x, y, z, intensity, frequency)
    """


def set_wii_nunchuk_shake(controller_id: int, x: float, y: float, z: float,
                      intensity: float, frequency: float, /) -> None:
    """
    Sets the wii nunchuk shake motion.
    This function is overridden by set_wii_nunchuk_acceleration.
    The override will hold for the current frame.
    :param controller_id: 0-based index of the controller
    :param x: x-component of the shake
    :param y: y-component of the shake
    :param z: z-component of the shake
    :param intensity: intensity of the shake (m)
    :param frequency: frequency of the shake (Hz)
    """


def get_wii_nunchuk_tilt(controller_id: int, /) \
        -> (float, float, float, float):
    """
    Returns the current wii nunchuk tilt motion state, as a vector (x, y, angle, velocity).
    The angle is in rad, and the velocity in rad/s
    :param controller_id: 0-based index of the controller
    :return: The current swing state as a tuple (x, y, angle, velocity)
    """


def set_wii_nunchuk_tilt(controller_id: int, x: float, y: float,
                     angle: float, velocity: float, /) -> None:
    """
    Sets the wii nunchuk tilt motion.
    This function is overridden by set_wii_nunchuk_angular_velocity.
    The override will hold for the current frame.
    :param controller_id: 0-based index of the controller
    :param x: x-component of the tilt
    :param y: y-component of the tilt
    :param angle: max tilt angle (rad)
    :param velocity: rotational velocity of the tilt (rad/s)
    """


def get_gba_buttons(controller_id: int, /) -> GBAInputs:
    """
    Retrieves the current input map for the given GameBoy Advance controller.
    All keys of :class:`GBAInputs` will be present in the returned dictionary.
    :param controller_id: 0-based index of the controller
    :return: dictionary describing the current input map
    """


def set_gba_buttons(controller_id: int, inputs: GBAInputs, /) -> None:
    """
    Sets the current input map for the given GameBoy Advance controller.

    All input keys omitted from the input map will not be applied
    and therefore stay in their current state.
    For example, you may only set the A button like this::

        controller.set_gba_buttons(0, {"A": True})

    The override will hold for the current frame.
    :param controller_id: 0-based index of the controller
    :param inputs: dictionary describing the input map
    """

class WUP028State(TypedDict, total=False):
    """
    The emulated Wii U GameCube Controller Adapter (WUP-028), for Wii homebrew
    that reads it through /dev/usb/hid. Its ports are fed from the GameCube
    controllers (port 1 from controller 0, and so on), so set_gc_buttons
    drives them too.
    Settable keys:
    plugged: the adapter is plugged in (takes effect within 50 ms).
    ports: which of its ports have a controller, bit 0 for port 1.
    wireless: which of those are WaveBirds.
    rumble_power: its grey plug (rumble power) is in.
    short_report_every: for testing, every how many reports one is cut
    short to 10 bytes (0: never).
    second_plugged, second_ports: a second adapter beside the first, its
    ports fed from the same GameCube controllers (port 2 by default).
    Read-only keys (get_wup028):
    attached: the Wii side opened it.
    started: the start command (0x13) came.
    rumble: the last rumble byte for each of the four ports.
    motors: whether each controller's motor is running: rumble reaches only
    a wired controller with the grey plug in, and 2 (hard stop) stops it.
    reports, short_reports, rumble_messages: counts since it was opened.
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
    second_plugged: bool
    second_attached: bool
    second_ports: int
    second_reports: int


def get_wup028() -> WUP028State:
    """
    Retrieves the emulated GameCube adapter's settings and what the Wii side
    did with it.

    :return: dictionary describing the adapter's state
    """


def set_wup028(state: WUP028State, /) -> None:
    """
    Changes the emulated GameCube adapter's settings: plugs it in or out,
    changes what's in its ports, and so on. Only the settable keys count;
    omitted keys stay as they are. For example::

        controller.set_wup028({"plugged": False})

    :param state: dictionary of the settings to change
    """
