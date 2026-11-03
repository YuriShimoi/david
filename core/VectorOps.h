#pragma once
#include "core/Vector.h"

namespace david {

// Edits of a cel's top-level objects through the picked ids (the Mouse tool's selection). Each returns false
// when nothing changed, and leaves sel on the objects it made or kept.
#pragma region [ Object edits ]
enum class EBoolOp : uint8_t { Union, Subtract, Intersect, Exclude };

bool removeObjects(std::vector<VecObject>& objs, std::vector<uint64_t>& sel);
// Copies 10 px down and to the right, picked in place of the originals
bool duplicateObjects(std::vector<VecObject>& objs, std::vector<uint64_t>& sel);
// One place up (dir 1) or down (-1) in the stack
bool orderObjects(std::vector<VecObject>& objs, const std::vector<uint64_t>& sel, int dir);
bool groupObjects(std::vector<VecObject>& objs, std::vector<uint64_t>& sel);
// Children come out in place, carrying the group's transform and opacity
bool ungroupObjects(std::vector<VecObject>& objs, std::vector<uint64_t>& sel);
// m applied around the center of the picked objects' box (flips, quarter turns)
bool transformObjects(std::vector<VecObject>& objs, const std::vector<uint64_t>& sel, const Affine& m);
bool objectsToPath(std::vector<VecObject>& objs, const std::vector<uint64_t>& sel);
// Fill areas combined (Clipper2) into one Path with the bottom object's style: union, the bottom one minus the
// others, the part all share, or the parts an odd number cover. Needs two objects or more.
bool booleanObjects(std::vector<VecObject>& objs, std::vector<uint64_t>& sel, EBoolOp op);
#pragma endregion

#pragma region [ Curve fitting ]
// Cubic Beziers through a polyline within error (Schneider's fit): corner nodes where it turns sharply,
// straight segments where it runs straight, smooth nodes elsewhere
Subpath fitCurve(std::vector<Point> pts, bool closed, double error);
#pragma endregion

}
