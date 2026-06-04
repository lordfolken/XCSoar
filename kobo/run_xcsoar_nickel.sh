#!/bin/sh
# Launcher for TARGET=KOBO_NICKEL on stock Nickel (do not kill nickel).
export LD_LIBRARY_PATH=/mnt/onboard/XCSoar/lib
cd /mnt/onboard
exec /mnt/onboard/XCSoar/xcsoar "$@"
