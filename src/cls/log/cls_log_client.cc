// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
#include <errno.h>

#include "cls/log/cls_log_ops.h"
#include "include/rados/librados.hpp"
#include "include/compat.h"


using std::vector;
using std::string;

using ceph::bufferlist;

using namespace librados;



void cls_log_add(librados::ObjectWriteOperation& op, vector<cls::log::entry>& entries, bool monotonic_inc)
{
  bufferlist in;
  cls::log::ops::add_op call;
  call.entries = entries;
  encode(call, in);
  op.exec("log", "add", in);
}

void cls_log_add(librados::ObjectWriteOperation& op, cls::log::entry& entry)
{
  bufferlist in;
  cls::log::ops::add_op call;
  call.entries.push_back(entry);
  encode(call, in);
  op.exec("log", "add", in);
}

void cls_log_add_prepare_entry(cls::log::entry& entry, ceph::real_time timestamp,
			       const string& section, const string& name, bufferlist& bl)
{
  entry.timestamp = timestamp;
  entry.section = section;
  entry.name = name;
  entry.data = bl;
}

void cls_log_add(librados::ObjectWriteOperation& op, ceph::real_time timestamp,
                 const string& section, const string& name, bufferlist& bl)
{
  cls::log::entry entry;

  cls_log_add_prepare_entry(entry, timestamp, section, name, bl);
  cls_log_add(op, entry);
}

void cls_log_trim(librados::ObjectWriteOperation& op, ceph::real_time from_time,
		  ceph::real_time to_time, const string& from_marker, const string& to_marker)
{
  bufferlist in;
  cls::log::ops::trim_op call;
  call.from_time = from_time;
  call.to_time = to_time;
  call.from_marker = from_marker;
  call.to_marker = to_marker;
  encode(call, in);
  op.exec("log", "trim", in);
}

int cls_log_trim(librados::IoCtx& io_ctx, const string& oid,
		 ceph::real_time from_time, ceph::real_time to_time,
                 const string& from_marker, const string& to_marker)
{
  bool done = false;

  do {
    ObjectWriteOperation op;

    cls_log_trim(op, from_time, to_time, from_marker, to_marker);

    int r = io_ctx.operate(oid, &op);
    if (r == -ENODATA)
      done = true;
    else if (r < 0)
      return r;

  } while (!done);


  return 0;
}

class LogListCtx : public ObjectOperationCompletion {
  vector<cls::log::entry>* entries;
  string *marker;
  bool *truncated;
public:
  LogListCtx(vector<cls::log::entry> *_entries, string *_marker, bool *_truncated) :
    entries(_entries), marker(_marker), truncated(_truncated) {}
  void handle_completion(int r, bufferlist& outbl) override {
    if (r >= 0) {
      cls::log::ops::list_ret ret;
      try {
        auto iter = outbl.cbegin();
        decode(ret, iter);
        if (entries)
          *entries = std::move(ret.entries);
        if (truncated)
          *truncated = ret.truncated;
        if (marker)
          *marker = std::move(ret.marker);
      } catch (ceph::buffer::error& err) {
        // nothing we can do about it atm
      }
    }
  }
};

void cls_log_list(librados::ObjectReadOperation& op, ceph::real_time from,
		  ceph::real_time to, const string& in_marker, int max_entries,
		  vector<cls::log::entry>& entries,
                  string *out_marker, bool *truncated)
{
  bufferlist inbl;
  cls::log::ops::list_op call;
  call.from_time = from;
  call.to_time = to;
  call.marker = in_marker;
  call.max_entries = max_entries;

  encode(call, inbl);

  op.exec("log", "list", inbl, new LogListCtx(&entries, out_marker, truncated));
}

class LogInfoCtx : public ObjectOperationCompletion {
  cls::log::header* header;
public:
  explicit LogInfoCtx(cls::log::header *_header) : header(_header) {}
  void handle_completion(int r, bufferlist& outbl) override {
    if (r >= 0) {
      cls::log::ops::info_ret ret;
      try {
        auto iter = outbl.cbegin();
        decode(ret, iter);
        if (header)
	  *header = ret.header;
      } catch (ceph::buffer::error& err) {
        // nothing we can do about it atm
      }
    }
  }
};

void cls_log_info(librados::ObjectReadOperation& op, cls::log::header *header)
{
  bufferlist inbl;
  cls::log::ops::info_op call;

  encode(call, inbl);

  op.exec("log", "info", inbl, new LogInfoCtx(header));
}
