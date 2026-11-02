# I G I E D - F N T - R G B 5 6 5 - L U M I N A N C E - 0 0 1

The FNT byte parser normalized RGB565 channels using shifts that produced an
incorrect alpha/luminance value for white pixels. The parser now scales each
channel to 8 bits before composing the atlas color. The synthetic regression
`FntParserSyntheticTest.ParsesMinimalRgb565FontAndGlyphMap` covers the fix.

Persistent mem0 summary: pending-interface (the available runtime exposes no
mem0 write operation); keep this status until the summary is written there.
