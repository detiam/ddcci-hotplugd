# ddcci-hotplugd
Made becasue ddcci-driver-linux is lack of [hotplug and auto-probing](https://gitlab.com/ddcci-driver-linux/ddcci-driver-linux/-/commit/f53b127ca9d7fc0969c0ee3499d8c55aebfe8116) support.  
This userspace program has ability to auto detect monitor using xrandr or udev(untested), then attach to it or re-attach to it when monitor status changed.

## How to use
```shell
> make && make install
> systemctl daemon-reload
> systemctl enable --now ddcci-hotplugd.service
```
For Archlinux: [ddcci-hotplugd](https://aur.archlinux.org/packages/ddcci-hotplugd)

## Environment variable

At [/etc/ddcci-hotplugd.env](./systemd/ddcci-hotplugd.env), also systemd unit configuration.
