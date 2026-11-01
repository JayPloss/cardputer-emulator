# Per-device power files

`tools/fit-power.py` writes one JSON file here for each device measured, named for the device
(`device-1.json`), from that device's power lab log. These files carry measurements of one person's
device, and a device's MAC address never appears in them, but they stay out of this public repo:
`.gitignore` keeps everything here except this note and `example-synthetic.json`, whose numbers come
from `tools/fake-power-log.py` and describe no real device. Emulator runs load a file with
`--power devices/device-1.json`. Without one, power is reported as uncalibrated, with no numbers.
