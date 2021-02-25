Gallium Nine SDL
====================================

.. image:: https://wiki.ixit.cz/_media/gallium-nine.png
    :target: https://wiki.ixit.cz/d3d9

About
-----
Gallium Nine allows to run any Direct3D 9 application with nearly no CPU overhead, which provides a smoother gaming experience and increased FPS.

Gallium Nine SDL, as the name implies, is an SDL port of the standalone version of the `WINE <https://www.winehq.org/>`_ parts of `Gallium Nine <https://github.com/iXit/wine>`_.

This allows Gallium Nine to be used in native applications that aren't running in a WINE environment.

Requirements
------------
* A Gallium based graphics driver (`Mesa 3D <https://www.mesa3d.org/>`_)
* Mesa's Gallium Nine state tracker (d3dadapter9.so)

Packages
--------
No packages are provided, the easiest way to use it in your project is to submodule it and include it in your ``CMakeLists.txt``.

Usage
-----
Link against the ``d3d9-nine`` static library and refer to main.cpp as an example on how to use the API.

Backends
--------
The DRI3 backend is the preferred one and has the lowest CPU and memory overhead.

As fallback for legacy platforms the DRI2 backend can be used, which has more CPU overhead and a bigger memory footprint.
The DRI2 fallback relies on mesa's EGL which provides EGLImages.

Debugging
---------
You can use the environment variable ``D3D_BACKEND`` to force one of the supported backends:

* dri3
* dri2

If not specified it prefers DRI3 over DRI2 if available.

