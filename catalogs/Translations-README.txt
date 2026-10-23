Translating AATextPrefs and AATextManager
=========================================

Both programs use locale.library catalogs. English is built in; Turkish
comes with the package. To add your language:

1. Copy aatextprefs.ct (or aatextmanager.ct) and fill in the empty
   line below each name with your translation. The English text is the
   comment below it. Set "## language" to your language's name as
   Locale prefs uses it (e.g. deutsch, français, italiano, polski).

   - Keep %s, %ld and \n, and keep the %-codes in their order.
   - A "_" marks the key of a button (_Save: S).
   - Save the file in your language's Amiga character set
     (ISO-8859-1 for most Western languages).

2. Build the catalog with FlexCat (Aminet dev/misc/FlexCat):

       FlexCat aatextprefs.cd aatextprefs.ct CATALOG aatextprefs.catalog

   or with CatComp:

       CatComp aatextprefs.cd aatextprefs.ct CATALOG aatextprefs.catalog

3. Copy it to LOCALE:Catalogs/<language>/ (make the drawer if needed)
   and start the program. "LANGUAGE=<language>" in the Shell selects
   it without changing Locale prefs.

The .cd files describe the texts; do not change them. Translations are
welcome as a pull request or issue on https://github.com/Sdursun/AAText
