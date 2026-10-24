# Host Qt 5 for the probe harness (probe/hosttest/run.sh), the UI simulator
# (tools/uisim.sh, which needs the QML modules the app imports), and patch +
# qmllint for tools/mkmod.sh.
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ make qtbase5-dev qtdeclarative5-dev qtdeclarative5-dev-tools \
        qml-module-qtquick2 qml-module-qtquick-window2 qml-module-qtquick-controls \
        qml-module-qtquick-dialogs qml-module-qtqml-models2 \
        qml-module-qtgraphicaleffects xvfb xauth libgl1-mesa-dri python3 patch
