TEMPLATE=subdirs
CONFIG += ordered
SUBDIRS = src \
    test

test.depends = src
