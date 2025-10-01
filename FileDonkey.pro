TEMPLATE=subdirs
CONFIG += ordered
SUBDIRS = app \
    core \
    test

app.depends = core
test.depends = core
