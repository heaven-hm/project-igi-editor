# Patrol freeze - root cause thread (CONTINUE HERE)

## Symptom
All guards frozen from t~150 (positions byte-identical across 5s samples).
Visually they play idle/default clips while render slides them = "panic" look.
User sees running; simulation says nobody moves even 1 unit.

## Hard evidence (AI-STATE log, level1 -play)
Every guard ends: cmd=<last>/N stop=1 started=1 route=0, current_node VALID
(95/101/105/257...). No [AI-GOTO] failure warnings fire => GoTo returns TRUE
with empty route. Two candidate paths inside GoTo():
  a) current_node == node instant-success branch (route.clear + true)
  b) GRAPH_EnumerateRoute returns {} silently for valid endpoints
Cursor then advances through Delay/End -> wrap-once stop (A7.3) = permanent freeze.

## Next steps (in order)
1. Log inside GoTo success paths: which branch taken, from,to, enumerated size,
   and first 3 route entries. Also log each guard's parsed patrol_commands once
   at registration (kind/operand list).
2. Verify WalkTo operand semantics: are operands NODE IDS in graph13/graph4019?
   Cross-check qsc text ("Walks to node id 9",2,9) against that graph's node count.
3. Check GRAPH_EnumerateRoute indexing vs retail: slot = from + to*maxNodes -
   validate against graph4019 by walking a known pair both directions; if flipped,
   fix indexing (destination-major was asserted, not proven).
4. Once routes resolve: remove the old fallback waypoint walker (it cycled ALL
   graph nodes = the original panic wandering) and keep authored-command-only.
5. Instrumentation left in tree: [AI-GOTO] warnings in GoTo, [AI-STATE] dumps
   every 150 ticks incl cmd/stop/route, [AI-ANIM] missing-clip warning. Remove
   or gate behind IGI_AI_DEBUG after fix.
