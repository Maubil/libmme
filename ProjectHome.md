# What? #
This library contains the API and low-level driver code to control the `mod_sim_exp` hardware IP core

  * Project website: http://opencores.org/project,mod_sim_exp

# Some more info #
The `mod_sim_exp` hardware IP core is meant to be used as a memory mapped peripheral in an embedded system. The hardware IP core is designed as a hardware accelerator for large-number modular arithmetic. E.g. it can compute _x<sup>a</sup> y<sup>b</sup>_ mod _m_. Where _x,y_ and _m_ are n-bit numbers and _a_ and _b_ are of arbitrary length.

The hardware accelerator is connected to a central (embedded) CPU over e.g. AXI bus. We assume that the CPU runs Linux and that the `mod_sim_exp` can be accessed as a UIO device.

This library uses both the UIO driver model and the GMP multi-precision library.
  * UIO info: https://www.kernel.org/doc/htmldocs/uio-howto.html
  * GMP project page: http://gmplib.org/

# New #
Source code for a test bench program that verifies the correct operation of the hardware IP core.