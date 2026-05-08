TEMPLATE=subdirs
CONFIG += ordered
SUBDIRS = core \
    app \
    test

app.depends = core
test.depends = core
