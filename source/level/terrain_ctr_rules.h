/******************************************************************************
 * @file    terrain_ctr_rules.h
 * @brief   The engine's terrain.ctr invariants, checkable without a Terrain.
 *
 * ORACLE (IGI-RE, read-only) - what the engine does with a .ctr, and what it
 * merely assumes:
 *
 *   src/terrain/terrain.c:128-130  Terrain_Load HOOK 0x4C6240
 *       t->ctr = Resource_Load(buf, &ctr_size);      // no header, no version
 *   src/terrain/terrain.c:142-143  same function
 *       for (i = 0; i < (int)(ctr_size >> 5); i++)
 *           t->ctr[i].leaf = (const short *)((const char *)t->ctr[i].leaf
 *                                            + (uintptr_t)t->cmd);
 *       => the file IS an array of 0x20-byte records and every record's
 *          0x1C field is a byte offset INTO terrain.cmd, added to the .cmd base.
 *   src/coll/colltypes.h:95-101    CollNode = child[8] u16, code[8] u8, mask u8,
 *       pad[3], leaf pointer.  sizeof == 0x20.
 *   src/coll/coll.c:113            Coll_InitCursor: c->node = t->ctr + 1;
 *       => node 1 is the root.
 *   src/coll/coll.c:230            Coll_Descend: node = nodes + node->child[child];
 *   src/coll/coll.c:281-330        Coll_GetLeafMaterial/Coll_LeafHit dereference
 *       node->leaf as a leaf module inside .cmd.
 *
 * The engine checks NONE of that: it has no error path for the .ctr at all
 * (a missing file makes Resource_Load return NULL with size 0, the loop runs
 * zero times, and the collision entry points bail out on a NULL terrain->ctr -
 * src/coll/coll.c:171).  The editor, unlike the engine, DEREFERENCES the array
 * (Terrain::RecursiveGetZ, Terrain::GenerateRenderCube, Level::GetDynCube), so
 * it has to check the four invariants above.  This header is that check, split
 * out so a host-lane test can run it on the real shipped files.
 *****************************************************************************/
#pragma once

#include <cstdint>

#include "level/terrain_files.h"

namespace igi {

/* The first record/mask/leaf triple that breaks one of the engine's
   assumptions.  `node_` is -1 when the whole array is usable. */
struct ctr_violation_s {
	int			node_ = -1;
	bool		child_ = false;	/* false: leaf offset, true: child index */
	int			slot_ = -1;		/* child slot 0..7, or -1 for the leaf */
	uint32_t	value_ = 0;
};

/* Checks, for every record, the two things the engine would dereference:
     * the leaf byte offset must lie inside terrain.cmd (Coll_LeafHit), and
     * every child index selected by the mask must lie inside the node array
       (Coll_Descend).
   Node 0 is skipped: the engine's descent starts at node 1 and node 0 is a
   zero-filled dummy in every shipped file (level 1..14 all begin with a
   zeroed 0x20-byte record). */
inline ctr_violation_s ValidateCtrNodes(const ctr_item_s* nodes, int count, int cmd_size) {
	ctr_violation_s bad;

	if (nodes == nullptr || count <= 0) {
		bad.node_ = 0;
		return bad;
	}

	for (int i = 1; i < count; ++i) {
		const ctr_item_s& node = nodes[i];

		/* The engine adds the .cmd base to this offset and dereferences it
		   (Terrain_Load 0x4C6240, src/terrain/terrain.c:142-143).  Offset 0 is
		   legal and is what interior records use in the retail levels.
		   The field is an unsigned byte offset (ctr_item_s::cmd_offset_ is
		   uint32_t, terrain_files.h:32), so the comparison must be unsigned:
		   casting to int turns the retail level-14 value 0xFFFFFFFF into -1,
		   which then reads as "inside" a 469644-byte .cmd and is not reported. */
		if (cmd_size < 0 || node.cmd_offset_ >= static_cast<uint32_t>(cmd_size)) {
			bad.node_ = i;
			bad.child_ = false;
			bad.value_ = node.cmd_offset_;
			return bad;
		}

		/* Coll_Descend (src/coll/coll.c:230) indexes the node array with these
		   values; validate every slot the mask says exists. */
		for (int slot = 0; slot < 8; ++slot) {
			if ((node.children_mask_ & (1u << slot)) == 0) {
				continue;
			}
			if ((int)node.children_[slot] < 0 || (int)node.children_[slot] >= count) {
				bad.node_ = i;
				bad.child_ = true;
				bad.slot_ = slot;
				bad.value_ = (uint32_t)(int)node.children_[slot];
				return bad;
			}
		}
	}

	return bad;
}

} // namespace igi
