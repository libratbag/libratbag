/*!@mainpage

libratbag
=========

libratbag is a configuration library for gaming mice. It provides a generic
way to access the various features exposed by these mice and abstracts away
hardware-specific and kernel-specific quirks.

Architecture
------------

libratbag has two main components, the front-end API and wrapper code, and
the back-end HW-specific drivers:

   +-----+    +-----+    +-----------+    +----------+
   | app | -> | API | -> | hw-driver | -> | protocol | -> device
   +-----+    +-----+    +-----------+    +----------+

The API layer is HW agnostic. Depend on the HW, the protocol may be part of
the driver implementation (e.g. etekcity) or a separate set of files
(HID++). Where the protocol is separate, the whole known protocol should be
implemented. The HW driver then only accesses the bits required for
libratbag. This allows us to optionally export the protocol as separate
library in the future, if other projects require it.

License
-------

libratbag is licensed under the MIT license.

> Permission is hereby granted, free of charge, to any person obtaining a
> copy of this software and associated documentation files (the "Software"),
> to deal in the Software without restriction, including without limitation
> the rights to use, copy, modify, merge, publish, distribute, sublicense,
> and/or sell copies of the Software, and to permit persons to whom the
> Software is furnished to do so, subject to the following conditions: [...]

See the COPYING file for the full license information.


*/
