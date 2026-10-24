# nw-installer crosstool plus what Qt 5.3's build needs on the host side:
# qtdeclarative runs a python script (masm create_regex_tables) while building.
FROM nw-crosstool:local
RUN apt-get update && apt-get install -y python3 && ln -sf /usr/bin/python3 /usr/bin/python
