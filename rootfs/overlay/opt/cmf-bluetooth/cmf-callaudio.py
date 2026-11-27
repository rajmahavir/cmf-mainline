#!/usr/bin/env python3
"""Drive the CMF call mixer through the GNOME Calls session API."""

import signal
import socket
import subprocess
import time
from pathlib import Path

from gi.repository import Gio, GLib

NAME = "org.mobian_project.CallAudio"
OBJECT = "/org/mobian_project/CallAudio"
PROPERTIES = "org.freedesktop.DBus.Properties"
MODEM_MANAGER = "org.freedesktop.ModemManager1"
MODEM_VOICE = f"{MODEM_MANAGER}.Modem.Voice"
MODEM_CALL = f"{MODEM_MANAGER}.Call"
ACTIVE_CALL = 4  # MM_CALL_STATE_ACTIVE
SPEECH_SOCKET = "/run/t2-speechd.sock"
ALSA = Path("/usr/bin")
MEDIA_PARAMS = Path("/proc/asound/card0/pcm0p/sub0/hw_params")
VOICE_STATUS = Path("/proc/asound/card0/pcm2p/sub0/status")
SPEAKER_MIXER = ("Speaker Switch", "I2SOUT4_CH1 DL0_CH1", "I2SOUT4_CH2 DL0_CH2")
API = f"""<node><interface name="{NAME}">
  <method name="SelectMode"><arg type="u" direction="in"/><arg type="b" direction="out"/></method>
  <method name="EnableSpeaker"><arg type="b" direction="in"/><arg type="b" direction="out"/></method>
  <method name="MuteMic"><arg type="b" direction="in"/><arg type="b" direction="out"/></method>
  <property name="AudioMode" type="u" access="read"/>
  <property name="SpeakerState" type="u" access="read"/>
  <property name="MicState" type="u" access="read"/>
</interface></node>"""


def speech(command):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(5)
        client.connect(SPEECH_SOCKET)
        client.sendall((command + "\n").encode())
        client.shutdown(socket.SHUT_WR)
        with client.makefile() as reply:
            lines = reply.read().splitlines()
    if lines[-1:] != ["ok"]:
        raise RuntimeError(" / ".join(lines) or "Speech service returned no status")
    if any("not acked" in line or "pending" in line for line in lines):
        raise RuntimeError("The modem did not acknowledge the speech change")
    return lines[:-1]


def mixer(control, value=None):
    amixer = str(ALSA / "amixer")
    if value is not None:
        subprocess.run(
            [amixer, "-q", "-c", "C1", "cset", f"name={control}", value],
            check=True, timeout=2,
        )
        return None
    output = subprocess.check_output(
        [amixer, "-c", "C1", "cget", f"name={control}"], text=True, timeout=2
    )
    return next(line.split("=", 1)[1] for line in output.splitlines()
                if line.startswith("  : values="))


def voice_open():
    return VOICE_STATUS.read_text().startswith("state: RUNNING")


def has_active_call():
    try:
        bus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
        reply = bus.call_sync(
            MODEM_MANAGER, "/org/freedesktop/ModemManager1",
            "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", None,
            GLib.VariantType.new("(a{oa{sa{sv}}})"),
            Gio.DBusCallFlags.NO_AUTO_START, 1000, None,
        )
    except GLib.Error:
        return False
    (objects,) = reply.unpack()
    for interfaces in objects.values():
        for path in interfaces.get(MODEM_VOICE, {}).get("Calls", ()):
            try:
                state = bus.call_sync(
                    MODEM_MANAGER, path, PROPERTIES, "Get",
                    GLib.Variant("(ss)", (MODEM_CALL, "State")),
                    GLib.VariantType.new("(v)"),
                    Gio.DBusCallFlags.NO_AUTO_START, 1000, None,
                )
            except GLib.Error:
                # The call can disappear between listing it and reading its state.
                continue
            if state.unpack() == (ACTIVE_CALL,):
                return True
    return False


class CallAudio:
    def __init__(self, connection):
        self.connection = connection
        self.properties = {"AudioMode": int(voice_open()), "SpeakerState": 0, "MicState": 1}
        self.clock_stream = None
        self.saved_mixer = {}

    def publish(self, name, value):
        if self.properties[name] == value:
            return
        self.properties[name] = value
        self.connection.emit_signal(
            None, OBJECT, PROPERTIES, "PropertiesChanged",
            GLib.Variant("(sa{sv}as)", (NAME, {name: GLib.Variant("u", value)}, [])),
        )

    def start_clock(self):
        self.saved_mixer = {control: mixer(control) for control in SPEAKER_MIXER}
        for control in self.saved_mixer:
            mixer(control, "on")
        # Keep the amplifier on the verified 44.1 kHz clock while the modem
        # Speech PCM runs at 48 kHz. This playback stream contains only silence.
        self.clock_stream = subprocess.Popen(
            [str(ALSA / "aplay"), "-q", "-D", "hw:C1,0", "-t", "raw",
             "-f", "S16_LE", "-r", "44100", "-c", "2", "/dev/zero"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        for _ in range(20):
            if self.clock_stream.poll() is not None:
                break
            if "rate: 44100 " in MEDIA_PARAMS.read_text():
                return
            time.sleep(0.05)
        raise RuntimeError("Could not start the 44.1 kHz speaker clock")

    def stop_clock(self):
        if self.clock_stream is not None:
            self.clock_stream.terminate()
            try:
                self.clock_stream.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.clock_stream.kill()
                self.clock_stream.wait()
            self.clock_stream = None
        for control, value in self.saved_mixer.items():
            mixer(control, value)
        self.saved_mixer.clear()

    def switch_speaker(self, enable):
        if enable == bool(self.properties["SpeakerState"]):
            return
        if not has_active_call() or not voice_open():
            self.clear_speaker()
            if enable:
                raise RuntimeError("The call voice stream is not open")
            return
        speech("call-stop")
        try:
            if enable:
                self.start_clock()
                speech("call-start --rate 48000 --device speaker")
            else:
                self.stop_clock()
                speech("call-start --rate 48000 --device earpiece")
        except Exception:
            self.clear_speaker()
            if has_active_call():
                speech("call-start --rate 48000 --device earpiece")
            raise
        if not has_active_call():
            speech("call-stop")
            self.clear_speaker()
            return
        self.publish("SpeakerState", int(enable))

    def clear_speaker(self):
        self.stop_clock()
        self.publish("SpeakerState", 0)

    def method(self, _connection, _sender, _path, _interface, name, args, invocation):
        (value,) = args.unpack()
        try:
            if name == "SelectMode":
                if value not in (0, 1):
                    raise ValueError("Unknown call audio mode")
                if value == 0:
                    self.clear_speaker()
                self.publish("AudioMode", value)
            elif name == "EnableSpeaker":
                self.switch_speaker(value)
            elif name == "MuteMic":
                speech("mute on" if value else "mute off")
                self.publish("MicState", int(not value))
        except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
            print(f"{name}: {error}", flush=True)
            invocation.return_value(GLib.Variant("(b)", (False,)))
        else:
            invocation.return_value(GLib.Variant("(b)", (True,)))

    def get_property(self, _connection, _sender, _path, _interface, name):
        return GLib.Variant("u", self.properties[name])

    def poll(self):
        if self.clock_stream and not voice_open():
            self.stop_clock()
            self.publish("SpeakerState", 0)
            self.publish("AudioMode", 0)
        return GLib.SOURCE_CONTINUE


def main():
    connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    audio = CallAudio(connection)
    interface = Gio.DBusNodeInfo.new_for_xml(API).interfaces[0]
    connection.register_object(OBJECT, interface, audio.method, audio.get_property, None)
    result = connection.call_sync(
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "RequestName", GLib.Variant("(su)", (NAME, 4)), GLib.VariantType.new("(u)"),
        Gio.DBusCallFlags.NONE, 5000, None,
    )
    if result.unpack() != (1,):
        raise RuntimeError("A call audio service already owns the session name")
    loop = GLib.MainLoop()
    GLib.timeout_add_seconds(1, audio.poll)
    GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, signal.SIGTERM, loop.quit)
    try:
        loop.run()
    finally:
        audio.clear_speaker()


if __name__ == "__main__":
    main()
