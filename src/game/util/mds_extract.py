#!/usr/bin/env python3
"""
mds_extract.py - MDS Skeletal Mesh Frame Extractor
====================================================
Parses a Wolfenstein RtCW MDS file, evaluates the skeleton at a specified
animation frame, skins all vertices, and exports each surface as a separate
Wavefront OBJ file suitable for decimation in pymeshlab.

Usage:
    python mds_extract.py <input.mds> <frame_number> <output_stem>

    The output stem is used as a base name. One OBJ is written per surface:
        <output_stem>_<surfacename>.obj

Examples:
    # Export the standing pose (alert_idle_2h first frame)
    python mds_extract.py body.mds 1633 body_stand

    # Export crouch-idle pose
    python mds_extract.py body.mds 1947 body_crouchidle

    # Export crouch-moving pose (mid frame of alert_crch_2h)
    python mds_extract.py body.mds 1924 body_crouchmove

    # This will produce e.g.:
    #   body_stand_l_legs.obj
    #   body_stand_u_body.obj
    #   body_stand_u_rthand.obj
    #   body_stand_u_lfthand.obj

Dependencies:
    Python 3.6+, no third-party packages required for this script.

Math reference:
    Bone evaluation:    model.c  MDL_CalcBone / MDL_CalcBoneLerp
    Vertex skinning:    tr_animation.c  RB_SurfaceAnim
                        LocalAddScaledMatrixTransformVectorTranslate
    Skinning formula:   out += weight * (bone.matrix * offset + bone.translation)
"""

import sys
import struct
import math
import os


# ---------------------------------------------------------------------------
# Constants matching qfiles.h
# ---------------------------------------------------------------------------

MDS_IDENT   = (ord('M') | (ord('D') << 8) | (ord('S') << 16) | (ord('W') << 24))
MDS_VERSION = 4

SHORT2ANGLE = 360.0 / 65536.0  # matches Quake3 SHORT2ANGLE macro


# ---------------------------------------------------------------------------
# Low-level binary helpers
# ---------------------------------------------------------------------------

def read_struct(fmt, data, offset):
    """Unpack a struct from data at offset, return (values_tuple, new_offset)."""
    size = struct.calcsize(fmt)
    values = struct.unpack_from(fmt, data, offset)
    return values, offset + size


def read_string(data, offset, length):
    """Read a null-terminated string from a fixed-length field."""
    raw = data[offset:offset + length]
    null = raw.find(b'\x00')
    if null >= 0:
        raw = raw[:null]
    return raw.decode('ascii', errors='replace'), offset + length


# ---------------------------------------------------------------------------
# Angle / matrix helpers  (mirroring model.c / tr_animation.c)
# ---------------------------------------------------------------------------

def short_to_angle(s):
    """Convert a signed short bone angle to degrees (SHORT2ANGLE)."""
    # Interpret as signed 16-bit
    if s > 32767:
        s -= 65536
    return s * SHORT2ANGLE


def angles_to_axis(pitch, yaw, roll):
    """
    Convert Euler angles (degrees, Quake convention: pitch/yaw/roll)
    to a 3x3 rotation matrix stored as a list of 3 rows.
    Matches AnglesToAxis() in q_math.c.
    Returns matrix as [[row0], [row1], [row2]] where each row is [x, y, z].
    """
    def deg2rad(d):
        return d * math.pi / 180.0

    sp = math.sin(deg2rad(pitch))
    cp = math.cos(deg2rad(pitch))
    sy = math.sin(deg2rad(yaw))
    cy = math.cos(deg2rad(yaw))
    sr = math.sin(deg2rad(roll))
    cr = math.cos(deg2rad(roll))

    # Quake axis convention: forward, left, up
    # forward (row 0)
    forward = [cp * cy, cp * sy, -sp]
    # left (row 1)
    left = [sr * sp * cy + cr * -sy,
            sr * sp * sy + cr * cy,
            sr * cp]
    # up (row 2)
    up = [cr * sp * cy + -sr * -sy,
          cr * sp * sy + -sr * cy,
          cr * cp]

    return [forward, left, up]


def local_angle_vector(pitch, yaw):
    """
    Compute direction vector from pitch/yaw angles (degrees).
    Matches LocalAngleVector() in model.c.
    Roll is always 0 for offset angle vectors.
    """
    yaw_rad   = yaw   * (math.pi * 2.0 / 360.0)
    pitch_rad = pitch * (math.pi * 2.0 / 360.0)
    sy = math.sin(yaw_rad);   cy = math.cos(yaw_rad)
    sp = math.sin(pitch_rad); cp = math.cos(pitch_rad)
    return [cp * cy, cp * sy, -sp]


def vec_normalize(v):
    """Normalize a 3-vector in place, return it."""
    length = math.sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2])
    if length > 1e-10:
        v[0] /= length; v[1] /= length; v[2] /= length
    return v


def slerp_normal(a, b, t):
    """
    Linearly interpolate two direction vectors and normalize.
    Matches SLerp_Normal() in model.c.
    """
    ft = 1.0 - t
    result = [a[0]*ft + b[0]*t,
              a[1]*ft + b[1]*t,
              a[2]*ft + b[2]*t]
    return vec_normalize(result)


def angle_normalize_180(a):
    """Normalize angle to [-180, 180]. Matches AngleNormalize180()."""
    a = a % 360.0
    if a >= 180.0:
        a -= 360.0
    return a


def mat_transform_add_scaled(offset, weight, matrix, translation, out):
    """
    Matches LocalAddScaledMatrixTransformVectorTranslate() in tr_animation.c:
        out += weight * (matrix * offset + translation)
    matrix is [[row0], [row1], [row2]], each row [x, y, z].
    Modifies out in place.
    """
    m = matrix
    ox, oy, oz = offset[0], offset[1], offset[2]
    tx, ty, tz = translation[0], translation[1], translation[2]

    out[0] += weight * (ox*m[0][0] + oy*m[0][1] + oz*m[0][2] + tx)
    out[1] += weight * (ox*m[1][0] + oy*m[1][1] + oz*m[1][2] + ty)
    out[2] += weight * (ox*m[2][0] + oy*m[2][1] + oz*m[2][2] + tz)


def local_vector_ma(org, dist, vec):
    """org + dist * vec  (LocalVectorMA)"""
    return [org[0] + dist*vec[0],
            org[1] + dist*vec[1],
            org[2] + dist*vec[2]]


# ---------------------------------------------------------------------------
# MDS data structures
# ---------------------------------------------------------------------------

class MDSHeader:
    SIZE = struct.calcsize('<2i64sff6i2i2i')  # ident,version,name,lodScale,lodBias,
                                               # numFrames,numBones,ofsFrames,ofsBones,
                                               # torsoParent,numSurfaces,ofsSurfaces,
                                               # numTags,ofsTags,ofsEnd
    def __init__(self, data, offset=0):
        (self.ident, self.version, name_raw,
         self.lodScale, self.lodBias,
         self.numFrames, self.numBones,
         self.ofsFrames, self.ofsBones,
         self.torsoParent,
         self.numSurfaces, self.ofsSurfaces,
         self.numTags, self.ofsTags,
         self.ofsEnd), _ = read_struct('<2i64sff6i2i2i', data, offset)

        null = name_raw.find(b'\x00')
        self.name = name_raw[:null].decode('ascii', errors='replace') if null >= 0 else name_raw.decode('ascii', errors='replace')


class MDSBoneInfo:
    """mdsBoneInfo_t - static bone hierarchy data."""
    FMT = '<64si3fi'   # name, parent, torsoWeight, parentDist, flags  (+ padding)
    # Actually: name[64], parent(int), torsoWeight(float), parentDist(float), flags(int)
    FMT = '<64siffi'
    SIZE = struct.calcsize(FMT)

    def __init__(self, data, offset):
        (name_raw, self.parent,
         self.torsoWeight, self.parentDist,
         self.flags), _ = read_struct(self.FMT, data, offset)
        null = name_raw.find(b'\x00')
        self.name = name_raw[:null].decode('ascii', errors='replace') if null >= 0 else ''
        self.isTorso   = self.torsoWeight != 0.0
        self.fullTorso = self.torsoWeight == 1.0


class MDSBoneFrameCompressed:
    """mdsBoneFrameCompressed_t - per-frame compressed bone pose."""
    FMT  = '<4h2h'   # angles[4] (short), ofsAngles[2] (short)
    SIZE = struct.calcsize(FMT)

    def __init__(self, data, offset):
        vals, _ = read_struct(self.FMT, data, offset)
        self.angles    = list(vals[0:4])   # [pitch, yaw, roll, unused]
        self.ofsAngles = list(vals[4:6])   # [pitch, yaw]


class MDSFrame:
    """mdsFrame_t header (before the variable bone array)."""
    # bounds[2][3], localOrigin[3], radius, parentOffset[3]
    FMT  = '<6f3ff3f'
    SIZE = struct.calcsize(FMT)

    def __init__(self, data, offset):
        vals, _ = read_struct(self.FMT, data, offset)
        self.bounds      = [[vals[0], vals[1], vals[2]],
                            [vals[3], vals[4], vals[5]]]
        self.localOrigin = [vals[6],  vals[7],  vals[8]]
        self.radius      = vals[9]
        self.parentOffset= [vals[10], vals[11], vals[12]]


class MDSWeight:
    """mdsWeight_t"""
    FMT  = '<if3f'   # boneIndex(int), boneWeight(float), offset[3](float)
    SIZE = struct.calcsize(FMT)

    def __init__(self, data, offset):
        (self.boneIndex, self.boneWeight,
         ox, oy, oz), _ = read_struct(self.FMT, data, offset)
        self.offset = [ox, oy, oz]


class MDSVertex:
    """
    mdsVertex_t - variable size due to weights[numWeights].
    Read with MDSVertex.read() which returns (vertex, bytes_consumed).
    """
    # Fixed header: normal[3], texCoords[2], numWeights(int), fixedParent(int), fixedDist(float)
    # fixedParent and fixedDist ARE present in the raw file as zero-initialized placeholders
    # (R_LoadMDS computes them at load time and writes back into the same memory)
    HEADER_FMT  = '<3f2fiif'
    HEADER_SIZE = struct.calcsize(HEADER_FMT)

    @classmethod
    def read(cls, data, offset):
        v = cls()
        (nx, ny, nz,
         v.u, v.v,
         v.numWeights,
         v.fixedParent,
         v.fixedDist), _ = read_struct('<3f2fiif', data, offset)
        # Note: fixedParent and fixedDist are computed at load time in R_LoadMDS,
        # but they ARE present in the struct in memory after load.
        # In the raw file they won't be set, so we just skip them for extraction.
        v.normal = [nx, ny, nz]

        weight_offset = offset + cls.HEADER_SIZE
        v.weights = []
        for _ in range(v.numWeights):
            w = MDSWeight(data, weight_offset)
            v.weights.append(w)
            weight_offset += MDSWeight.SIZE

        v.byte_size = weight_offset - offset
        return v, weight_offset

    def __init__(self):
        self.normal = [0.0, 0.0, 0.0]
        self.u = 0.0; self.v = 0.0
        self.numWeights = 0
        self.weights = []
        self.byte_size = 0


class MDSTriangle:
    FMT  = '<3i'
    SIZE = struct.calcsize(FMT)

    def __init__(self, data, offset):
        vals, _ = read_struct(self.FMT, data, offset)
        self.indexes = list(vals)


class MDSSurface:
    """mdsSurface_t header."""
    # ident(int), name[64], shader[64], shaderIndex(int), minLod(int),
    # ofsHeader(int), numVerts(int), ofsVerts(int), numTriangles(int),
    # ofsTriangles(int), ofsCollapseMap(int), numBoneReferences(int),
    # ofsBoneReferences(int), ofsEnd(int)
    FMT  = '<i64s64siiiiiiiiiii'
    SIZE = struct.calcsize(FMT)

    def __init__(self, data, offset):
        self.offset = offset
        (self.ident,
         name_raw, shader_raw,
         self.shaderIndex, self.minLod, self.ofsHeader,
         self.numVerts, self.ofsVerts,
         self.numTriangles, self.ofsTriangles,
         self.ofsCollapseMap,
         self.numBoneReferences, self.ofsBoneReferences,
         self.ofsEnd), _ = read_struct(self.FMT, data, offset)

        null = name_raw.find(b'\x00')
        self.name = name_raw[:null].decode('ascii', errors='replace') if null >= 0 else ''


# ---------------------------------------------------------------------------
# MDS parser
# ---------------------------------------------------------------------------

class MDSFile:
    def __init__(self, filepath):
        with open(filepath, 'rb') as f:
            self.data = f.read()
        self._parse()

    def _parse(self):
        data = self.data
        self.header = MDSHeader(data, 0)
        hdr = self.header

        if hdr.ident != MDS_IDENT:
            raise ValueError(f"Not an MDS file (ident={hdr.ident:#010x}, expected={MDS_IDENT:#010x})")
        if hdr.version != MDS_VERSION:
            raise ValueError(f"Wrong MDS version ({hdr.version}, expected {MDS_VERSION})")

        print(f"MDS: '{hdr.name}'  frames={hdr.numFrames}  bones={hdr.numBones}  surfaces={hdr.numSurfaces}")

        # Frame size formula from model.c R_LoadMDS and tr_animation.c R_CullModel:
        #   sizeof(mdsFrame_t) - sizeof(mdsBoneFrameCompressed_t) + numBones * sizeof(mdsBoneFrameCompressed_t)
        self.frameSize = MDSFrame.SIZE + hdr.numBones * MDSBoneFrameCompressed.SIZE

        # Parse bone info array (static hierarchy)
        self.boneInfos = []
        offset = hdr.ofsBones
        for i in range(hdr.numBones):
            bi = MDSBoneInfo(data, offset)
            self.boneInfos.append(bi)
            offset += MDSBoneInfo.SIZE
            # print(f"  bone[{i}] '{bi.name}'  parent={bi.parent}  torsoWeight={bi.torsoWeight:.2f}  parentDist={bi.parentDist:.4f}")

        # Parse surfaces
        self.surfaces = []
        offset = hdr.ofsSurfaces
        for i in range(hdr.numSurfaces):
            surf = MDSSurface(data, offset)
            self.surfaces.append(surf)
            self._parse_surface(surf)
            offset += surf.ofsEnd

    def _parse_surface(self, surf):
        data = self.data
        base = surf.offset

        # Triangles
        surf.triangles = []
        tri_offset = base + surf.ofsTriangles
        for i in range(surf.numTriangles):
            t = MDSTriangle(data, tri_offset)
            surf.triangles.append(t)
            tri_offset += MDSTriangle.SIZE

        # Vertices (variable stride)
        surf.vertices = []
        vert_offset = base + surf.ofsVerts
        for i in range(surf.numVerts):
            v, vert_offset = MDSVertex.read(data, vert_offset)
            surf.vertices.append(v)

        # Bone references for this surface
        surf.boneRefs = []
        bref_offset = base + surf.ofsBoneReferences
        for i in range(surf.numBoneReferences):
            (idx,), bref_offset = read_struct('<i', data, bref_offset)
            surf.boneRefs.append(idx)

        print(f"  surface '{surf.name}'  verts={surf.numVerts}  tris={surf.numTriangles}  boneRefs={surf.numBoneReferences}")

    def get_frame(self, frame_num):
        """Return (MDSFrame header, list of MDSBoneFrameCompressed) for the given frame."""
        hdr = self.header
        if frame_num < 0 or frame_num >= hdr.numFrames:
            raise ValueError(f"Frame {frame_num} out of range [0, {hdr.numFrames-1}]")

        offset = hdr.ofsFrames + frame_num * self.frameSize
        frame  = MDSFrame(self.data, offset)

        bones_offset = offset + MDSFrame.SIZE
        compressed_bones = []
        for i in range(hdr.numBones):
            cb = MDSBoneFrameCompressed(self.data, bones_offset)
            compressed_bones.append(cb)
            bones_offset += MDSBoneFrameCompressed.SIZE

        return frame, compressed_bones


# ---------------------------------------------------------------------------
# Skeleton evaluator
# ---------------------------------------------------------------------------

class SkeletonEvaluator:
    """
    Evaluates all bone transforms for a single frame (no lerp).
    This is the non-lerp path from MDL_CalcBone in model.c,
    with backlerp=0 / frontlerp=1 and torsoFrame == frame
    (i.e. same frame for both legs and torso).
    """

    def __init__(self, mds_file):
        self.mds = mds_file

    def evaluate(self, frame_num):
        """
        Evaluate skeleton at frame_num.
        Returns list of mdsBoneFrame_t equivalents: dicts with 'matrix' and 'translation'.
        """
        mds    = self.mds
        hdr    = mds.header
        frame, cbones = mds.get_frame(frame_num)

        # We use the same frame for both legs and torso (no torso/legs split needed
        # for a static collision mesh snapshot — the torsoWeight blending still
        # applies within the frame's own bone data).
        cBoneList      = cbones   # legs frame bones
        cBoneListTorso = cbones   # torso frame bones (same frame)

        boneInfos = mds.boneInfos
        bones     = [None] * hdr.numBones   # evaluated mdsBoneFrame_t equivalents

        # Build full bone list in parent-first order using recursive traversal
        # (mirrors MDL_RecursiveBoneListAdd)
        boneList = []
        visited  = [False] * hdr.numBones

        def add_bone(bi):
            if visited[bi]:
                return
            parent = boneInfos[bi].parent
            if parent >= 0:
                add_bone(parent)
            visited[bi] = True
            boneList.append(bi)

        for i in range(hdr.numBones):
            add_bone(i)

        # Evaluate each bone in parent-first order
        # Mirrors MDL_CalcBone (no-lerp path) in model.c
        torsoParentOffset = [0.0, 0.0, 0.0]

        for boneNum in boneList:
            bi   = boneInfos[boneNum]
            cb   = cBoneList[boneNum]
            ctb  = cBoneListTorso[boneNum]

            # --- Rotation ---
            if bi.fullTorso:
                # Use torso frame angles directly
                pitch = short_to_angle(ctb.angles[0])
                yaw   = short_to_angle(ctb.angles[1])
                roll  = short_to_angle(ctb.angles[2])
            else:
                # Use legs frame angles
                pitch = short_to_angle(cb.angles[0])
                yaw   = short_to_angle(cb.angles[1])
                roll  = short_to_angle(cb.angles[2])

                if bi.isTorso:
                    # Blend legs and torso angles by torsoWeight
                    t_pitch = short_to_angle(ctb.angles[0])
                    t_yaw   = short_to_angle(ctb.angles[1])
                    t_roll  = short_to_angle(ctb.angles[2])

                    for ang, tang in [(pitch, t_pitch), (yaw, t_yaw), (roll, t_roll)]:
                        pass  # rebuild below properly

                    def blend_angle(a, ta, w):
                        diff = angle_normalize_180(ta - a)
                        return a + w * diff

                    pitch = blend_angle(pitch, t_pitch, bi.torsoWeight)
                    yaw   = blend_angle(yaw,   t_yaw,   bi.torsoWeight)
                    roll  = blend_angle(roll,  t_roll,  bi.torsoWeight)

            matrix = angles_to_axis(pitch, yaw, roll)

            # --- Translation ---
            if bi.parent >= 0:
                parentBone = bones[bi.parent]

                if bi.fullTorso:
                    oa_pitch = short_to_angle(ctb.ofsAngles[0])
                    oa_yaw   = short_to_angle(ctb.ofsAngles[1])
                else:
                    oa_pitch = short_to_angle(cb.ofsAngles[0])
                    oa_yaw   = short_to_angle(cb.ofsAngles[1])

                    if bi.isTorso:
                        # Blend legs and torso offset angles
                        t_oa_pitch = short_to_angle(ctb.ofsAngles[0])
                        t_oa_yaw   = short_to_angle(ctb.ofsAngles[1])

                        legs_dir  = local_angle_vector(oa_pitch, oa_yaw)
                        torso_dir = local_angle_vector(t_oa_pitch, t_oa_yaw)
                        dir_vec   = slerp_normal(legs_dir, torso_dir, bi.torsoWeight)
                        translation = local_vector_ma(parentBone['translation'], bi.parentDist, dir_vec)

                        bones[boneNum] = {'matrix': matrix, 'translation': translation}
                        if boneNum == hdr.torsoParent:
                            torsoParentOffset = translation[:]
                        continue

                dir_vec     = local_angle_vector(oa_pitch, oa_yaw)
                translation = local_vector_ma(parentBone['translation'], bi.parentDist, dir_vec)

            else:
                # Root bone — use frame parentOffset
                translation = frame.parentOffset[:]

            bones[boneNum] = {'matrix': matrix, 'translation': translation}
            if boneNum == hdr.torsoParent:
                torsoParentOffset = translation[:]

        return bones


# ---------------------------------------------------------------------------
# Vertex skinner
# ---------------------------------------------------------------------------

def skin_surface(surface, bones):
    """
    Apply bone transforms to all vertices in a surface.
    Returns list of [x, y, z] world-space positions.

    Matches the vertex loop in RB_SurfaceAnim (tr_animation.c):
        out += boneWeight * (bone.matrix * offset + bone.translation)
    """
    positions = []
    for v in surface.vertices:
        pos = [0.0, 0.0, 0.0]
        for w in v.weights:
            bone = bones[w.boneIndex]
            mat_transform_add_scaled(w.offset, w.boneWeight, bone['matrix'], bone['translation'], pos)
        positions.append(pos)
    return positions


# ---------------------------------------------------------------------------
# OBJ exporter
# ---------------------------------------------------------------------------

def export_surface_obj(surface, positions, output_path, source_name):
    """
    Export a single skinned surface as a Wavefront OBJ file.
    positions is the list of [x,y,z] from skin_surface().
    """
    with open(output_path, 'w') as f:
        f.write(f"# Exported by mds_extract.py\n")
        f.write(f"# Source: {source_name}  Surface: {surface.name}\n")
        f.write(f"# Vertices: {len(positions)}  Faces: {len(surface.triangles)}\n\n")

        for v in positions:
            f.write(f"v {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}\n")

        f.write("\n")

        for tri in surface.triangles:
            # OBJ indices are 1-based
            f.write(f"f {tri.indexes[0]+1} {tri.indexes[1]+1} {tri.indexes[2]+1}\n")

    print(f"  '{surface.name}': {len(positions)} verts, {len(surface.triangles)} faces -> '{output_path}'")


def export_obj_per_surface(mds_file, bones, output_stem):
    """
    Export each surface of the MDS file as a separate OBJ.
    Output files are named: <output_stem>_<surfacename>.obj
    Returns list of (surface_name, output_path) for all exported surfaces.
    """
    exported = []
    for surf in mds_file.surfaces:
        positions   = skin_surface(surf, bones)
        output_path = f"{output_stem}_{surf.name}.obj"
        export_surface_obj(surf, positions, output_path, mds_file.header.name)
        exported.append((surf.name, output_path))
    return exported


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main():
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(1)

    mds_path    = sys.argv[1]
    frame_num   = int(sys.argv[2])
    output_stem = sys.argv[3]

    if not os.path.isfile(mds_path):
        print(f"Error: file not found: '{mds_path}'")
        sys.exit(1)

    print(f"Loading '{mds_path}'...")
    mds = MDSFile(mds_path)

    print(f"Evaluating skeleton at frame {frame_num}...")
    evaluator = SkeletonEvaluator(mds)
    bones     = evaluator.evaluate(frame_num)

    print(f"Exporting per-surface OBJs (stem: '{output_stem}')...")
    exported = export_obj_per_surface(mds, bones, output_stem)

    print(f"Done. {len(exported)} surfaces exported.")


if __name__ == '__main__':
    main()
