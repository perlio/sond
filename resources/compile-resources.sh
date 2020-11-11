#!/bin/sh
glib-compile-resources --target=../src/10init/icons.c --sourcedir=icons --generate-source --c-name icons resources.xml
glib-compile-resources --target=../src/10init/icons.h --sourcedir=icons --generate-header --c-name icons resources.xml