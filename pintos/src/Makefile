BUILD_SUBDIRS = threads userprog vm filesys

all::
	@echo "Run 'make' in subdirectories: $(BUILD_SUBDIRS)."
	@echo "This top-level make has only 'clean' targets."

CLEAN_SUBDIRS = $(BUILD_SUBDIRS) examples utils

clean::
	for d in $(CLEAN_SUBDIRS); do $(MAKE) -C $$d $@; done
	rm -f TAGS tags

distclean:: clean
	find . -name '*~' -exec rm '{}' \;

TAGS_SUBDIRS = $(BUILD_SUBDIRS) devices lib
TAGS_SOURCES = find $(TAGS_SUBDIRS) -name \*.[chS] -print

TAGS::
	etags --members `$(TAGS_SOURCES)`

tags::
	ctags -T --no-warn `$(TAGS_SOURCES)`

cscope.files::
	$(TAGS_SOURCES) > cscope.files

cscope:: cscope.files
	cscope -b -q -k

submission:
	@echo "Packaging Pintos Project 2 submission for 240449A..."
	git archive --format=zip -o ../240449A_Lab2_submission.zip HEAD
	git archive --format=tar.gz -o ../240449A_Lab2_submission.tar.gz HEAD
	@echo "SUCCESS: Created ../240449A_Lab2_submission.zip and ../240449A_Lab2_submission.tar.gz"

