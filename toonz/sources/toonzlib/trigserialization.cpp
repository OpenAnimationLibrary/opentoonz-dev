#include "toonz/trigserialization.h"
#include "tstream.h"
#include "texception.h"

void saveRigBone(TOStream &os, const TRigBone &bone) {
  os.child("bone") << int(bone.enabled) << bone.tip.x << bone.tip.y;
  for (const auto &c : bone.constraints) {
    os.openChild("constraint");
    os.child("type") << int(c.type);
    os.child("endpoint") << int(c.endpoint);
    if (!c.targetId.empty()) os.child("target") << c.targetId;
    os.child("options") << int(c.enabled) << c.enforcement << c.firstFrame
                        << c.lastFrame;
    os.child("offset") << c.offset.x << c.offset.y << c.angleOffset;
    os.child("limits") << c.minimum.x << c.minimum.y << c.maximum.x
                       << c.maximum.y << c.minValue << c.maxValue;
    for (const auto &key : c.enforcementKeys)
      os.child("key") << key.first << key.second;
    os.closeChild();
  }
}

TRigBone loadRigBone(TIStream &is) {
  TRigBone data;
  std::string tag;
  while (is.matchTag(tag)) {
    if (tag == "bone") {
      int enabled;
      is >> enabled >> data.tip.x >> data.tip.y;
      data.enabled = enabled != 0;
    } else if (tag == "constraint") {
      if (data.constraints.size() >= 128)
        throw TException("Too many rig constraints");
      TRigConstraint c;
      std::string child;
      while (is.matchTag(child)) {
        if (child == "type") {
          int type;
          is >> type;
          if (type < 0 || type >= TRigConstraint::TypeCount)
            throw TException("Unknown rig constraint type");
          c.type = static_cast<TRigConstraint::Type>(type);
        } else if (child == "endpoint") {
          int endpoint;
          is >> endpoint;
          if (endpoint < 0 || endpoint >= TRigConstraint::EndpointCount)
            throw TException("Unknown rig endpoint type");
          c.endpoint = static_cast<TRigConstraint::Endpoint>(endpoint);
        } else if (child == "target")
          is >> c.targetId;
        else if (child == "options") {
          int enabled;
          is >> enabled >> c.enforcement >> c.firstFrame >> c.lastFrame;
          c.enabled = enabled != 0;
        } else if (child == "offset")
          is >> c.offset.x >> c.offset.y >> c.angleOffset;
        else if (child == "limits")
          is >> c.minimum.x >> c.minimum.y >> c.maximum.x >> c.maximum.y >>
              c.minValue >> c.maxValue;
        else if (child == "key") {
          if (c.enforcementKeys.size() >= 100000)
            throw TException("Too many rig enforcement keys");
          int frame;
          double value;
          is >> frame >> value;
          c.enforcementKeys[frame] = value;
        } else {
          is.skipCurrentTag();
          continue;
        }
        is.matchEndTag();
      }
      if (!c.isValid()) throw TException("Invalid rig constraint");
      data.constraints.push_back(c);
    } else {
      is.skipCurrentTag();
      continue;
    }
    is.matchEndTag();
  }
  if (!data.isValid()) throw TException("Invalid rig bone");
  return data;
}
