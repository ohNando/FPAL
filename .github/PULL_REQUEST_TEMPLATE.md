## What and why

<!-- One or two sentences: what changes and why. -->

## Checked

- [ ] `make test` and `make ENROLL=0 test` pass
- [ ] If C code changed: also run under the sanitizers (see CONTRIBUTING.md)
- [ ] The Egis wire bytes are unchanged (exact-byte and golden tests), or the change is deliberate and explained
- [ ] Nothing that writes to the sensor ended up in the login code (`pam_fpal.so`, `fpald`)
- [ ] No real template ids, serial numbers or USB traces in the diff, tests or description

## New or changed sensor driver

<!-- Skip if not about a driver. See docs/adding-a-sensor.md. -->

- [ ] Output of `fpal report --markdown` is attached
- [ ] Verified on real hardware: yes / no (say which parts)
