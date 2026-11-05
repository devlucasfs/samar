const std = @import("std");

const c = @cImport({
    @cInclude("stdio.h");
    @cInclude("stdlib.h");
    @cInclude("unistd.h");
    @cInclude("sys/stat.h");
});

const KEY: u32 = 1024;
const MAX_NS = 64;
const MAX_KEYS = 16;
const MAX_FIELDS = 32;
const MAX_ATTEMPTS = 3;
const STAMP_MAX = 48;

const Status = enum(c_int) { unset = 0, defined = 1 };

var path_buf: [4097]u8 = [_]u8{0} ** 4097;
var path_len: usize = 0;
var status: Status = .unset;

var out_path: [4097]u8 = [_]u8{0} ** 4097;
var out_set: bool = false;

var in_buf: [65536]u8 = undefined;
var out: Out = .{};

var last_err: c_int = 0;
var err_name: [*:0]const u8 = "none";

export fn set(path: [*:0]const u8) c_int {
    const s = std.mem.span(path);
    if( s.len >= path_buf.len ) return -1;
    @memcpy(path_buf[0..s.len], s);
    path_buf[s.len] = 0;
    path_len = s.len;
    status = .defined;
    return 0;
}

export fn setoutput(path: [*:0]const u8) c_int {
    const s = std.mem.span(path);
    if( s.len >= out_path.len ) return -1;
    @memcpy(out_path[0..s.len], s);
    out_path[s.len] = 0;
    out_set = true;
    return 0;
}

export fn status_str() [*:0]const u8 {
    return switch (status) {
        .defined => "defined",
        .unset => "unset",
    };
}

export fn last_error() c_int {
    return last_err;
}
export fn error_name() [*:0]const u8 {
    return err_name;
}

export fn prepare() c_int {
    return generate();
}

export fn reworkCheck() bool {
    if( status != .defined ) {
        last_err = -1;
        return false;
    }

    if(! needsRework() ) return false;

    var attempt: u32 = 0;
    while(attempt < MAX_ATTEMPTS) : (attempt += 1) {
        if( generate() == 0 ) return true;
        if( last_err != -2 and last_err != -5 ) break;
    }
    return false;
}

fn readInput() isize {
    if (status != .defined) return -1;
    const f = c.fopen(&path_buf, "rb") orelse return -2;
    const n = c.fread(&in_buf, 1, in_buf.len, f);
    _ = c.fclose(f);
    if( n == in_buf.len ) return -3;
    return @intCast(n);
}

fn inputStamp(buf: *[STAMP_MAX]u8) ?[]const u8 {
    var st: c.struct_stat = undefined;
    if( c.stat(&path_buf, &st) != 0 ) return null;
    return std.fmt.bufPrint(buf, "{d}.{d:0>9}", .{ st.st_mtim.tv_sec, st.st_mtim.tv_nsec }) catch null;
}

fn outputPath(tmp: *[4097 + 5]u8) ?[*:0]const u8 {
    if( out_set ) return @ptrCast(&out_path);
    const s = std.fmt.bufPrintZ(tmp, "{s}.eva", .{path_buf[0..path_len]}) catch return null;
    return s.ptr;
}

fn writeOutput() c_int {
    var tmp: [4097 + 5]u8 = undefined;
    const p = outputPath(&tmp) orelse return -5;
    const f = c.fopen(p, "wb") orelse return -5;
    const w = c.fwrite(&out.buf, 1, out.len, f);
    _ = c.fclose(f);
    return if (w == out.len) 0 else -5;
}

fn generate() c_int {
    const rc = generateInner();
    last_err = rc;
    return rc;
}

fn generateInner() c_int {
    if( status != .defined ) return -1;

    var sb: [STAMP_MAX]u8 = undefined;
    const stamp = inputStamp(&sb) orelse return -2;

    const n = readInput();
    if( n < 0 ) return @intCast(n);
    const len: usize = @intCast(n);

    out.len = 0;
    build(stamp, in_buf[0..len], &out) catch |e| {
        err_name = @errorName(e).ptr;
        return -4;
    };

    return writeOutput();
}

fn needsRework() bool {
    var sb: [STAMP_MAX]u8 = undefined;
    const stamp = inputStamp(&sb) orelse return true;

    var tmp: [4097 + 5]u8 = undefined;
    const p = outputPath(&tmp) orelse return true;
    const f = c.fopen(p, "rb") orelse return true;

    var head: [512]u8 = undefined;
    const n = c.fread(&head, 1, head.len, f);
    _ = c.fclose(f);

    const last = readLastChange(head[0..n]) orelse return true;
    return !std.mem.eql(u8, last, stamp);
}

fn readLastChange(txt: []const u8) ?[]const u8 {
    var it = std.mem.splitScalar(u8, txt, '\n');
    while(it.next()) |raw| {
        const line = std.mem.trim(u8, raw, " \t\r");
        if( line.len == 0 or line[0] == '\'' ) continue;
        if( line[0] == '@' ) return null;

        const colon = std.mem.indexOfScalar(u8, line, ':') orelse return null;
        const key = std.mem.trim(u8, line[0..colon], " \t");
        if(! std.mem.eql(u8, key, "last_change") ) continue;

        const val = std.mem.trim(u8, line[colon + 1 ..], " \t");
        if( val.len < 2 or val[0] != '"' or val[val.len - 1] != '"' ) return null;
        return val[1 .. val.len - 1];
    }
    return null;
}

const ParseError = error{
    Overflow,
    NoHeader,
    BadHeader,
    BadLine,
    WrongType,
    ReservedField,
    DuplicateField,
    DuplicateNamespace,
    DuplicateKey,
    PatternMismatch,
    MissingField,
};

const Out = struct {
    buf: [65536]u8 = undefined,
    len: usize = 0,

    fn put(self: *Out, s: []const u8) ParseError!void {
        if( self.len + s.len > self.buf.len ) return error.Overflow;
        @memcpy(self.buf[self.len..][0..s.len], s);
        self.len += s.len;
    }

    fn quoted(self: *Out, s: []const u8) ParseError!void {
        try self.put("\"");
        for(s) |ch| switch(ch) {
            '"' => try self.put("\\\""),
            '\\' => try self.put("\\\\"),
            else => try self.put((&ch)[0..1]),
        };
        try self.put("\"");
    }
};

fn namespaceId(s: []const u8, key: u32) u32 {
    var acc: u32 = key;
    for(s) |b| acc = (acc ^ b) *% 16777619;
    return acc;
}

fn build(stamp: []const u8, src: []const u8, o: *Out) ParseError!void {
    try o.put("last_change: ");
    try o.quoted(stamp);
    try o.put("\n");
    try translate(src, o);
}

const Section = struct {
    tools: bool = false,
    linking: bool = false,
    compiling: bool = false,
    seen: [MAX_FIELDS][]const u8 = undefined,
    n: usize = 0,

    fn mark(self: *Section, key: []const u8) ParseError!void {
        for( self.seen[0..self.n] ) |k|
            if( std.mem.eql(u8, k, key) ) return error.DuplicateField;

        if( self.n == self.seen.len ) return error.Overflow;
        self.seen[self.n] = key;
        self.n += 1;
    }

    fn check(self: Section) ParseError!void {
        if( (! self.tools) or (! self.linking) or (! self.compiling ) ) return error.MissingField;
    }
};

const Header = struct { triple: []const u8, pattern: []const u8 };

fn stripComment(line: []const u8) []const u8 {
    var in_q = false;
    var i: usize = 0;
    while(i < line.len) : (i += 1) {
        const ch = line[i];
        if( in_q ) {
            if( ch == '\\' ) {
                i += 1;
            } else if( ch == '"' ) {
                in_q = false;
            }
        } else if( ch == '"' ) {
            in_q = true;
        } else if( ch == '/' and i + 1 < line.len and line[i + 1] == '/' ) {
            return line[0..i];
        }
    }
    return line;
}

fn translate(src: []const u8, o: *Out) ParseError!void {
    var sec: ?Section = null;
    var ids: [MAX_NS]u32 = undefined;
    var n_ids: usize = 0;

    var it = std.mem.splitScalar(u8, src, '\n');
    while(it.next()) |raw| {
        const line = std.mem.trim(u8, stripComment(raw), " \t\r");
        if( line.len == 0 ) continue;

        if( line[0] == '#' ) {
            const h = parseHeader(line) orelse return error.BadHeader;
            if( sec ) |s| try s.check();

            const id = namespaceId(h.triple, KEY);
            for( ids[0..n_ids] ) |x| if (x == id) return error.DuplicateNamespace;
            if( n_ids == ids.len ) return error.Overflow;
            ids[n_ids] = id;
            n_ids += 1;

            try emitHeader(h, id, o);
            sec = .{};
            continue;
        }

        if( sec == null ) return error.NoHeader;
        try field(line, &sec.?, o);
    }

    if( sec ) |s| {
        try s.check();
    } else return error.NoHeader;
}

fn parseHeader(line: []const u8) ?Header {
    var tok = std.mem.tokenizeAny(u8, line[1..], " \t");
    const triple = tok.next() orelse return null;
    const kw = tok.next() orelse return null;
    const pat = tok.next() orelse return null;
    if( tok.next() != null ) return null;
    if(! std.mem.eql(u8, kw, "as") ) return null;
    if( pat.len < 2 or pat[0] != '@' ) return null;
    if( std.mem.indexOfScalar(u8, triple, ':') == null ) return null;
    return .{ .triple = triple, .pattern = pat[1..] };
}

fn emitHeader(h: Header, id: u32, o: *Out) ParseError!void {
    var nb: [16]u8 = undefined;
    const s = std.fmt.bufPrint(&nb, "{d}", .{id}) catch unreachable;

    if (o.len > 0) try o.put("\n");
    try o.put("@");
    try o.put(s);
    try o.put("\ntarget: ");
    try patternMap(h, o);
    try o.put("\n");
}

fn patternMap(h: Header, o: *Out) ParseError!void {
    var keys = std.mem.splitScalar(u8, h.pattern, ':');
    var vals = std.mem.splitScalar(u8, h.triple, ':');
    var seen: [MAX_KEYS][]const u8 = undefined;
    var n: usize = 0;

    try o.put("{ ");
    while(true) {
        const k = keys.next();
        const v = vals.next();
        if( k == null and v == null ) break;
        if( k == null or v == null ) return error.PatternMismatch;

        const key = std.mem.trim(u8, k.?, " \t");
        const val = std.mem.trim(u8, v.?, " \t");
        if (key.len == 0 or val.len == 0) return error.BadLine;

        for (seen[0..n]) |x| if (std.mem.eql(u8, x, key)) return error.DuplicateKey;
        if (n == seen.len) return error.Overflow;
        seen[n] = key;
        n += 1;

        if (n > 1) try o.put(" ");
        try bareKey(key, o);
        try o.put(": ");
        try o.quoted(val);
    }
    try o.put(" }");
}

const Kind = enum { tools, string, list, boolean, str_or_bool };

fn kindOf(key: []const u8) Kind {
    if( std.mem.eql(u8, key, "tools") ) return .tools;
    if( std.mem.eql(u8, key, "linking") or std.mem.eql(u8, key, "compiling") ) return .string;
    if( std.mem.eql(u8, key, "scripts") ) return .list;
    if( std.mem.eql(u8, key, "monitor") ) return .boolean;
    return .str_or_bool;
}

fn isBool(s: []const u8) bool {
    return std.mem.eql(u8, s, "true") or std.mem.eql(u8, s, "false");
}

fn isBare(s: []const u8) bool {
    if( s.len == 0 ) return false;
    for(s) |ch| {
        if (!std.ascii.isAlphanumeric(ch) and ch != '_' and ch != '-') return false;
    }
    return true;
}

fn bareKey(s: []const u8, o: *Out) ParseError!void {
    if( isBare(s) ) try o.put(s) else try o.quoted(s);
}

fn quotedValue(val: []const u8, o: *Out) ParseError!void {
    if( val.len < 2 or val[0] != '"' or val[val.len - 1] != '"' ) return error.WrongType;
    try o.quoted(val[1 .. val.len - 1]);
}

fn field(line: []const u8, sec: *Section, o: *Out) ParseError!void {
    const colon = std.mem.indexOfScalar(u8, line, ':') orelse return error.BadLine;
    const key = std.mem.trim(u8, line[0..colon], " \t");
    const val = std.mem.trim(u8, line[colon + 1 ..], " \t");
    if( val.len == 0 or !isBare(key) ) return error.BadLine;
    if( std.mem.eql(u8, key, "pattern") or std.mem.eql(u8, key, "last_change") )
        return error.ReservedField;

    const kind = kindOf(key);
    try sec.mark(key);
    if( kind == .tools ) sec.tools = true;
    if( std.mem.eql(u8, key, "linking") ) sec.linking = true;
    if( std.mem.eql(u8, key, "compiling") ) sec.compiling = true;

    try o.put(key);
    try o.put(": ");

    switch (kind) {
        .tools => {
            try o.put("[ ");
            var first = true;
            var it = std.mem.splitScalar(u8, val, ',');
            while(it.next()) |item| {
                var parts = std.mem.splitScalar(u8, std.mem.trim(u8, item, " \t"), '`');
                while(parts.next()) |part| {
                    const p = std.mem.trim(u8, part, " \t");
                    if( p.len == 0 ) return error.BadLine;
                    if(! first ) try o.put(", ");
                    first = false;
                    try o.quoted(p);
                }
            }
            try o.put(" ]");
        },
        .string => try quotedValue(val, o),
        .list => try stringArray(val, ',', o),
        .boolean => {
            if(! isBool(val) ) return error.WrongType;
            try o.put(val);
        },
        .str_or_bool => {
            if( isBool(val) ) try o.put(val) else try quotedValue(val, o);
        },
    }
    try o.put("\n");
}

fn stringArray(s: []const u8, sep: u8, o: *Out) ParseError!void {
    try o.put("[");
    var it = std.mem.splitScalar(u8, s, sep);
    var first = true;
    while(it.next()) |part| {
        const p = std.mem.trim(u8, part, " \t");
        if (p.len == 0) return error.BadLine;
        if (!first) try o.put(", ");
        first = false;
        try o.quoted(p);
    }
    try o.put("]");
}

const body =
    \\tools: as`package
    \\linking: "ld"
    \\compiling: "as"
;

fn writeFile(path: [*:0]const u8, data: []const u8) !void {
    const f = c.fopen(path, "wb") orelse return error.OpenFailed;
    defer _ = c.fclose(f);
    if( c.fwrite(data.ptr, 1, data.len, f) != data.len ) return error.WriteFailed;
}

fn readFile(path: [*:0]const u8, buf: []u8) ![]u8 {
    const f = c.fopen(path, "rb") orelse return error.OpenFailed;
    defer _ = c.fclose(f);
    const n = c.fread(buf.ptr, 1, buf.len, f);
    return buf[0..n];
}

test "translate" {
    const src =
        \\# x86_64:linux as @arch:os
        \\tools: as`package, ld`package
        \\linking: "ld $files -o $output"
        \\compiling: "as -m64 $file -o $output"
    ;
    var o: Out = .{};
    try translate(src, &o);

    var ib: [16]u8 = undefined;
    const id = try std.fmt.bufPrint(&ib, "{d}", .{namespaceId("x86_64:linux", KEY)});
    var eb: [512]u8 = undefined;
    const want = try std.fmt.bufPrint(
        &eb,
        "@{s}\ntarget: {{ arch: \"x86_64\" os: \"linux\" }}\ntools: [[\"as\", \"package\"], [\"ld\", \"package\"]]\nlinking: \"ld $files -o $output\"\ncompiling: \"as -m64 $file -o $output\"\n",
        .{id},
    );
    try std.testing.expectEqualStrings(want, o.buf[0..o.len]);
}

test "build põe last_change no topo, fora de namespace" {
    var o: Out = .{};
    try build("123.000000456", "# x86_64:linux as @arch:os\n" ++ body, &o);
    try std.testing.expect(std.mem.startsWith(u8, o.buf[0..o.len], "last_change: \"123.000000456\"\n\n@"));
}

test "readLastChange" {
    try std.testing.expectEqualStrings(
        "123.000000456",
        readLastChange("' comentário\nlast_change: \"123.000000456\"\n\n@1\n").?,
    );
    try std.testing.expect(readLastChange("@1\nlast_change: \"x\"\n") == null);
    try std.testing.expect(readLastChange("") == null);
}

test "comentários com //" {
    const src =
        \\// comentário de linha inteira
        \\# x86_64:linux as @arch:os // no fim do cabeçalho
        \\tools: as`package // no fim do campo
        \\linking: "ld http://x" // // dentro de aspas é preservado
        \\compiling: "as"
    ;
    var o: Out = .{};
    try translate(src, &o);
    try std.testing.expect(std.mem.indexOf(u8, o.buf[0..o.len], "linking: \"ld http://x\"\n") != null);
    try std.testing.expect(std.mem.indexOf(u8, o.buf[0..o.len], "no fim") == null);
}

test "# que não é cabeçalho falha" {
    var o: Out = .{};
    try std.testing.expectError(error.BadHeader, translate("# oi\n" ++ body, &o));
}

test "namespace sem campo obrigatório falha" {
    const src =
        \\# x86_64:linux as @arch:os
        \\tools: as`package
        \\linking: "ld"
    ;
    var o: Out = .{};
    try std.testing.expectError(error.MissingField, translate(src, &o));
}

test "cada namespace exige os três campos" {
    const src =
        \\# x86_64:linux as @arch:os
        \\tools: as`package
        \\linking: "ld"
        \\compiling: "as"
        \\# aarch64:linux as @arch:os
        \\tools: as`package
        \\linking: "ld"
    ;
    var o: Out = .{};
    try std.testing.expectError(error.MissingField, translate(src, &o));
}

test "dois namespaces completos" {
    const src =
        \\# x86_64:linux as @arch:os
        \\tools: as`package
        \\linking: "ld"
        \\compiling: "as"
        \\# aarch64:linux as @arch:os
        \\tools: as`package
        \\linking: "ld"
        \\compiling: "as"
    ;
    var o: Out = .{};
    try translate(src, &o);
    try std.testing.expectEqual(@as(usize, 2), std.mem.count(u8, o.buf[0..o.len], "target: "));
}

test "pattern: quantidade diferente de partes falha" {
    var o: Out = .{};
    try std.testing.expectError(
        error.PatternMismatch,
        translate("# x86_64:linux as @arch\n" ++ body, &o),
    );
    try std.testing.expectError(
        error.PatternMismatch,
        translate("# x86_64:linux as @arch:os:abi\n" ++ body, &o),
    );
}

test "pattern: chave repetida falha" {
    var o: Out = .{};
    try std.testing.expectError(
        error.DuplicateKey,
        translate("# x86_64:linux as @arch:arch\n" ++ body, &o),
    );
}

test "pattern: três partes, na ordem, sem vírgula" {
    var o: Out = .{};
    try translate("# x86_64:linux:gnu as @arch:os:abi\n" ++ body, &o);
    try std.testing.expect(std.mem.indexOf(
        u8,
        o.buf[0..o.len],
        "target: { arch: \"x86_64\" os: \"linux\" abi: \"gnu\" }",
    ) != null);
}

test "campos opcionais" {
    const src =
        \\# x86_64:linux as @arch:os
        \\tools: as`package
        \\linking: "ld"
        \\compiling: "as"
        \\scripts: test1, test2, test3
        \\monitor: true
        \\flash: "openocd -f board.cfg"
        \\objdump: false
        \\extra: "guardado"
        \\debug: true
    ;
    var o: Out = .{};
    try translate(src, &o);
    const txt = o.buf[0..o.len];
    try std.testing.expect(std.mem.indexOf(u8, txt, "scripts: [\"test1\", \"test2\", \"test3\"]\n") != null);
    try std.testing.expect(std.mem.indexOf(u8, txt, "monitor: true\n") != null);
    try std.testing.expect(std.mem.indexOf(u8, txt, "flash: \"openocd -f board.cfg\"\n") != null);
    try std.testing.expect(std.mem.indexOf(u8, txt, "objdump: false\n") != null);
    try std.testing.expect(std.mem.indexOf(u8, txt, "extra: \"guardado\"\n") != null);
    try std.testing.expect(std.mem.indexOf(u8, txt, "debug: true\n") != null);
}

test "tipos dos campos opcionais" {
    const h = "# x86_64:linux as @arch:os\n" ++ body ++ "\n";
    var o: Out = .{};
    try std.testing.expectError(error.WrongType, translate(h ++ "monitor: \"sim\"", &o));
    try std.testing.expectError(error.WrongType, translate(h ++ "monitor: True", &o));
    try std.testing.expectError(error.WrongType, translate(h ++ "flash: 5", &o));
    try std.testing.expectError(error.WrongType, translate(h ++ "objdump: a, b", &o));
    try std.testing.expectError(error.WrongType, translate(h ++ "extra: 42", &o));
    try std.testing.expectError(error.BadLine, translate(h ++ "scripts: a,,b", &o));
    try std.testing.expectError(error.BadLine, translate(h ++ "chave ruim: \"x\"", &o));
}

test "opcionais: repetido e reservado falham" {
    const h = "# x86_64:linux as @arch:os\n" ++ body ++ "\n";
    var o: Out = .{};
    try std.testing.expectError(error.DuplicateField, translate(h ++ "monitor: true\nmonitor: false", &o));
    try std.testing.expectError(error.ReservedField, translate(h ++ "pattern: \"x\"", &o));
    try std.testing.expectError(error.ReservedField, translate(h ++ "last_change: \"x\"", &o));
}

test "set + status" {
    try std.testing.expectEqualStrings("unset", std.mem.span(status_str()));
    try std.testing.expectEqual(@as(c_int, 0), set("/tmp/x"));
    try std.testing.expectEqualStrings("defined", std.mem.span(status_str()));
}

test "foo só recompila quando o Sake muda (last_change)" {
    const good =
        \\# x86_64:linux as @arch:os
        \\tools: as`package, ld`package
        \\linking: "ld $files -o $output"
        \\compiling: "as -m64 $file -o $output"
    ;
    _ = c.unlink("/tmp/samar_test_out.eva");
    try writeFile("/tmp/samar_test_in", good);
    try std.testing.expectEqual(@as(c_int, 0), set("/tmp/samar_test_in"));
    try std.testing.expectEqual(@as(c_int, 0), setoutput("/tmp/samar_test_out.eva"));

    try std.testing.expect(reworkCheck());
    try std.testing.expect(!reworkCheck());

    var rb: [1024]u8 = undefined;
    const txt = try readFile("/tmp/samar_test_out.eva", &rb);
    try std.testing.expect(std.mem.startsWith(u8, txt, "last_change: \""));
    try std.testing.expect(readLastChange(txt) != null);

    _ = c.usleep(50_000);
    try writeFile("/tmp/samar_test_in", good ++ "\n// editado\n");
    try std.testing.expect(reworkCheck());
    try std.testing.expect(!reworkCheck());

    _ = c.usleep(50_000);
    try writeFile("/tmp/samar_test_in", "# x86_64:linux as @arch:os\ntools: as`package\n");
    try std.testing.expect(!reworkCheck());
    try std.testing.expectEqual(@as(c_int, -4), last_error());
    try std.testing.expectEqualStrings("MissingField", std.mem.span(error_name()));
}

test "real: <cwd>/Sake -> output do argumento" {
    const outp = c.getenv("SAMAR_OUT") orelse return error.SkipZigTest;
    const outz: [*:0]const u8 = @ptrCast(outp);

    var cwd: [4096]u8 = undefined;
    if (c.getcwd(&cwd, cwd.len) == null) return error.GetCwdFailed;
    var ib: [4096 + 8]u8 = undefined;
    const inp = try std.fmt.bufPrintZ(&ib, "{s}/test/Sake", .{std.mem.sliceTo(&cwd, 0)});

    try std.testing.expectEqual(@as(c_int, 0), set(inp.ptr));
    try std.testing.expectEqual(@as(c_int, 0), setoutput(outz));

    if (prepare() != 0) {
        std.debug.print(
            "falhou: input={s} err={d} ({s})\n",
            .{ inp, last_error(), std.mem.span(error_name()) },
        );
        return error.GenerateFailed;
    }

    var rb: [65536]u8 = undefined;
    const txt = try readFile(outz, &rb);
    try std.testing.expectEqualStrings(out.buf[0..out.len], txt);
    try std.testing.expect(std.mem.startsWith(u8, txt, "last_change: \""));
    try std.testing.expect(std.mem.indexOf(u8, txt, "target: ") != null);

    try std.testing.expect(!reworkCheck());

    std.debug.print("\n--- {s} ---\n{s}\n", .{ std.mem.span(outz), txt });
}
