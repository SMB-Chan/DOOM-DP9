.SUFFIXES:

ifeq ($(strip $(DEVKITSH4)),)
$(error "Please set DEVKITSH4 in your environment. export DEVKITSH4=<path to sdk>")
endif
include $(DEVKITSH4)/exword_rules

TARGET		:= doom
SOURCEDIR	:= src
APPID		:= DOOMX
APPMOD		:= $(TARGET).d01
APPTITLE	:= DOOM EX-word
MODNAME		:= doom01

CFILES		:= $(wildcard $(SOURCEDIR)/*.c) $(wildcard $(SOURCEDIR)/libc/*.c)
SFILES		:= $(wildcard $(SOURCEDIR)/libc/*.s)
OBJECTS		:= $(CFILES:.c=.o) $(SFILES:.s=.o)

LDFLAGS := -nostdlib -L$(DEVKITPRO)/libdataplus/lib -ldataplus -lgraphics -lsh4a -lgcc
CFLAGS  := -fno-builtin -I$(DEVKITPRO)/libdataplus/include -I$(SOURCEDIR) -I$(SOURCEDIR)/libc/include -O2 -ffunction-sections -fdata-sections -Wall
ASFLAGS := -m4-nofpu

LDFLAGS += -Wl,--gc-sections

.PHONY: clean app html

app: $(TARGET).d01 html
	@mkdir -p build/ja/$(APPID)
	@cp $(TARGET).d01 build/ja/$(APPID)/
	@cp html/ja/*.htm build/ja/$(APPID)/
	@touch build/ja/$(APPID)/fileinfo.cji
	@echo built build/ja/$(APPID)

$(TARGET).d01: $(TARGET).elf

$(TARGET).elf: $(OBJECTS)

html:
	@mkdir -p html/ja
	@for n in 3 4 5 6 7 8 9 10 11 12 13 14 15; do \
		printf 'dicapl="700"\r\nid="DOOMX"\r\nhistory="OFF"\r\nkjump="OFF"\r\n\r\n#switch;\r\n' > html/ja/infodp$$n.htm; \
		printf 'more_srch="OFF"\r\nnarrow_srch="OFF"\r\norder_srch="OFF"\r\ncenter="OFF"\r\ntts_lang="of"\r\naddin="ON,doom.d01"\r\njp_lang="of"\r\npic_prev="OFF"\r\nlist_idec="OFF"\r\n' > html/ja/infogt$$n.htm; \
	done
	@printf '<html>\r\n<head>\r\n<title>DOOM EX-word</title>\r\n<meta name="soft" content="OFF">\r\n</head>\r\n<body>\r\n</body>\r\n</html>\r\n' > html/ja/diction.htm

clean:
	@rm -fr build html $(OBJECTS) $(TARGET).elf $(TARGET).d01 $(TARGET).elf.map
