
funky_mods = bricklayer autowarp autoturret record sgcompat $(EXTRA_FUNKY_MODS)
funky_libs = $(ZLIB_LIB) -lm

$(eval $(call dl_template,funky))

# generated file for bricklayer
$(call tobuild, letters.inc): $(builddir) $(SCRIPTS)/processfont.py $(SCRIPTS)/banner.font
	$(PYTHON) $(SCRIPTS)/processfont.py $(SCRIPTS)/banner.font $@

# dist: public

