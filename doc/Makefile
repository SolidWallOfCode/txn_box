# Makefile for Sphinx documentation

.PHONY: help dirhtml singlehtml

srcdir        ?= .
SPHINXBUILD    = python3 -m sphinx
PAPER         ?= letter
BUILDDIR      ?= docbuild
JAVA          ?= java
PLANTUML_JAR  ?= $(HOME)/bin/plantuml.jar
PLANTUML_ARGS ?=
REALPATH       = $(if $(WINDIR), cygpath --absolute --windows, realpath)

SBUILD = $(SPHINXBUILD) ${PAPEROPT_letter}
PLANTUML = $(JAVA) -jar $(PLANTUML_JAR) $(PLANTUML_ARGS)
IMAGEDIR = $(srcdir)/pix
BUILD_IMAGEDIR = $(BUILDDIR)/html/_images
UMLDIR = $(srcdir)/uml

$(IMAGEDIR)/%.svg : $(UMLDIR)/%.plantuml
	$(PLANTUML) $< -tsvg -o $(shell $(REALPATH) $(IMAGEDIR))

html: uml ext/local-config.py
	$(SBUILD) -d $(BUILDDIR)/doctrees -b html $(srcdir) $(BUILDDIR)/html
	@echo
	@echo "Build finished. The HTML pages are in $(BUILDDIR)/html."

uml: $(IMAGEDIR)/txn_box_config_schema.svg

clean:
	-rm -rf html warn.log
	-rm -rf $(BUILDDIR)/doctrees $(BUILDDIR)/html
	-rm uml/*.svg

ext/local-config.py : Makefile
	@echo "plantuml = '$(JAVA) -jar $(PLANTUML_JAR)'" > ext/local-config.py
	@echo "plantuml_output_format = 'svg'" >> ext/local-config.py
