# XCSoar Container Image

This container image when built, will compile XCSoar for several targets in a
clean room environment.

## Currently Supported Targets

Targets:
  - UNIX (linux & co)
  - UNIX-SDL (Software Rendering for X11 forward)
  - ANDROID
  - PC
  - KOBO
  - DOCS

## Instructions

The container itself is readonly. The build results will appear in
`./XCSoar/output/<platform>`.

To run the prebuilt container interactivly:
```bash
podman run --userns=keep-id \
    --mount type=bind,source="$(pwd)",target=/opt/xcsoar/XCSoar \
    --volume xcsoar-cache:/opt/xcsoar/.cache/ccache \
    -it ghcr.io/xcsoar/xcsoar/xcsoar-build:latest
```

To run the ANDROID build:
```bash
podman run --userns=keep-id \
    --mount type=bind,source="$(pwd)",target=/opt/xcsoar/XCSoar \
    --volume xcsoar-cache:/opt/xcsoar/.cache/ccache \
    -it ghcr.io/xcsoar/xcsoar/xcsoar-build:latest xcsoar-compile ANDROID
```

To build the container:
```bash
podman build \
    --file ide/docker/Dockerfile \
    -t xcsoar/xcsoar-build:latest ./ide/
```

### Running XCSoar as a GUI application from the container

Sometimes your runtime environment diverges too far from the build environment
to be able to execute the binary natively.  In this case you can start XCSoar
inside the container and let it be displayed on your X11 Server:

```bash
podman run --userns=keep-id \
    --mount type=bind,source="$(pwd)",target=/opt/xcsoar/XCSoar \
    --volume="$HOME/.Xauthority:/root/.Xauthority:rw" \
    --volume="$HOME/.xcsoar/:/opt/xcsoar/.xcsoar" \
    --env="DISPLAY" --net=host \
    -it localhost/xcsoar/xcsoar-build:latest ./XCSoar/output/UNIX/bin/xcsoar
```
