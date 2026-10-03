# BUILD

## MuPDF bauen

Unverändert aus src/zond/ToDo.c übernommen (Stand 03.10.2026).

```text
 Build mupdf:

 - git clone --recursive git://git.ghostscript.com/mupdf.git
 - cd mupdf
 - git submodule update --init
 - Makefile für  mupdf modifizieren:

 Zeile 75: Compiler-Optionen -mavx -Wno-incompatible-pointer-types ergänzen
 "CC_CMD = $(QUIET_CC) $(MKTGTDIR) ; $(CC) $(CFLAGS) -mavx -Wno-incompatible-pointer-types -MMD -MP -o $@ -c $<"

Änderung Rules war beim letzten Kompilieren (16.06.25) nicht erforderlich
 # --- Rules ---

 $(OUT)/%.a :
 $(file >arscript.sh,@$(AR_CMD))
 bash -x arscript.sh
 $(RANLIB_CMD)


 - mingw32-make libs
```
