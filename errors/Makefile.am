## Process this file with automake to produce Makefile.in
#
# $Id: Makefile.am 14372 2009-11-18 10:37:49Z adrian.chadd $
#

errordir = $(datadir)/errors


##dist-hook eveything

DEFAULT_ERROR_DIR	= $(errordir)

INSTALL_LANGUAGES	= templates @ERR_LANGUAGES@

TEMPLATE_LANGUAGES = English

LANGUAGES	= \
		Armenian \
		Azerbaijani \
		Bulgarian \
		Catalan \
		Czech \
		Danish \
		Dutch \
		English \
		Estonian \
		Finnish \
		French \
		German \
		Greek \
		Hebrew \
		Hungarian \
		Italian \
		Japanese \
		Korean \
		Lithuanian \
		Polish \
		Portuguese \
		Romanian \
		Russian-1251 \
		Russian-koi8-r \
		Serbian \
		Simplify_Chinese \
		Slovak \
		Spanish \
		Swedish \
		Traditional_Chinese \
		Turkish \
		Ukrainian-1251 \
		Ukrainian-koi8-u \
		Ukrainian-utf8

all-local:
	@for l in $(TEMPLATE_LANGUAGES); do ./build.pl src/$$l $$l || exit 1; done

install-data-local:
	@for l in $(INSTALL_LANGUAGES); do \
		$(mkinstalldirs) $(DESTDIR)$(DEFAULT_ERROR_DIR)/$$l && \
		for f in $(srcdir)/$$l/ERR_*; do \
			echo "$(INSTALL_DATA) $$f $(DESTDIR)$(DEFAULT_ERROR_DIR)/$$l"; \
			$(INSTALL_DATA) $$f $(DESTDIR)$(DEFAULT_ERROR_DIR)/$$l; \
		done \
	done

uninstall-local:
	@for l in $(INSTALL_LANGUAGES); do \
		for f in $(srcdir)/$$l/ERR_*; do \
	        	rm -f $(DESTDIR)$(DEFAULT_ERROR_DIR)/$$l/`basename $$f`; \
		done \
	done

# undocumented hack.  You can use this target to create multi-lingual
# error pages.  For example:
#
#	make ADDLANG=English DESTLANG=French addlang
#
# by Andres Kroonmaa <andre@mail.lbi.ee>
#
addlang: all
	-@if test -d $(srcdir)/$(ADDLANG); then \
	if test -d $(DEFAULT_ERROR_DIR)/$(DESTLANG); then \
	cd $(srcdir)/$(ADDLANG)/ ; \
	for f in ERR_*; do \
		if test -f $(DEFAULT_ERROR_DIR)/$(DESTLANG)/$$f ; then \
			echo "appending $(ADDLANG)/$$f"; \
			echo "<HR>" >> $(DEFAULT_ERROR_DIR)/$(DESTLANG)/$$f ; \
			cat $$f >> $(DEFAULT_ERROR_DIR)/$(DESTLANG)/$$f; \
		fi; \
	done; \
	fi \
	fi

dist-hook:
	@ for lang in $(LANGUAGES) templates; do \
	  if test "$$lang" = .; then :; else \
	    test -d $(distdir)/$$lang \
	    || mkdir $(distdir)/$$lang \
	    || exit 1; \
	    cp -p $(srcdir)/$$lang/ERR_* $(distdir)/$$lang \
	    || exit 1; \
	    if test -f $(srcdir)/$$lang/README; then \
	      cp -p $(srcdir)/$$lang/README $(distdir)/$$lang \
	      || exit 1; \
	    fi; \
	  fi; \
	done
