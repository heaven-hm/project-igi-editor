# IGIED-FNT-RGB565-LUMINANCE-001

Status: fixed

An all-white RGB565 FNT atlas pixel was decoded with alpha 249 because the
component expansion used approximate shifts before averaging. The decoder now
normalizes each RGB565 channel to 0 through 255 before calculating luminance.
`FntParserSyntheticTest.ParsesMinimalRgb565FontAndGlyphMap` verifies the
white pixel remains fully opaque.

Mem0 summary pending: no mem0 persistence interface is available in this
workspace session.
