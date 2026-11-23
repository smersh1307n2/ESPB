using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace ESPB_Runtime
{
    public static class FastSymbols
    {
        // Make public for API access
        public static Dictionary<string, ushort> ParseSymFilePublic(string path) => ParseSymFile(path);

        private static string StripComments(string src)
        {
            // Removes // and /* */ comments while preserving string/char literals.
            var sb = new StringBuilder(src.Length);
            bool inStr = false;
            bool inChr = false;
            bool esc = false;
            for (int i = 0; i < src.Length; i++)
            {
                char ch = src[i];

                if (inStr)
                {
                    sb.Append(ch);
                    if (esc) esc = false;
                    else if (ch == '\\') esc = true;
                    else if (ch == '"') inStr = false;
                    continue;
                }
                if (inChr)
                {
                    sb.Append(ch);
                    if (esc) esc = false;
                    else if (ch == '\\') esc = true;
                    else if (ch == '\'') inChr = false;
                    continue;
                }

                if (ch == '"') { inStr = true; sb.Append(ch); continue; }
                if (ch == '\'') { inChr = true; sb.Append(ch); continue; }

                // Line comment
                if (ch == '/' && i + 1 < src.Length && src[i + 1] == '/')
                {
                    i += 2;
                    while (i < src.Length && src[i] != '\n') i++;
                    if (i < src.Length) sb.Append('\n');
                    continue;
                }

                // Block comment
                if (ch == '/' && i + 1 < src.Length && src[i + 1] == '*')
                {
                    i += 2;
                    while (i + 1 < src.Length && !(src[i] == '*' && src[i + 1] == '/')) i++;
                    i++; // skip '*'
                    continue;
                }

                sb.Append(ch);
            }
            return sb.ToString();
        }

        // Parses *.sym in format:
        //   ESPB_SYM("name", expr)
        //   ESPB_SYM_OPT(cfg, "name", expr)
        // Returns mapping: name -> index (0-based)
        public static Dictionary<string, ushort> ParseSymFile(string path)
        {
            var text = File.ReadAllText(path, Encoding.UTF8);
            text = StripComments(text);
            var map = new Dictionary<string, ushort>(StringComparer.Ordinal);

            int i = 0;
            ushort index = 0;
            while (true)
            {
                // Find next macro occurrence: ESPB_SYM/ESPB_SYM_OPT or SYM/SYM_OPT
                int posEspb = text.IndexOf("ESPB_SYM", i, StringComparison.Ordinal);
                int posSym2 = text.IndexOf("SYM", i, StringComparison.Ordinal);

                int posSym;
                bool isEspb;
                if (posEspb < 0) { posSym = posSym2; isEspb = false; }
                else if (posSym2 < 0) { posSym = posEspb; isEspb = true; }
                else if (posEspb <= posSym2) { posSym = posEspb; isEspb = true; }
                else { posSym = posSym2; isEspb = false; }

                if (posSym < 0) break;

                // Determine which macro
                string baseName = isEspb ? "ESPB_SYM" : "SYM";
                bool isOpt = text.AsSpan(posSym).StartsWith(baseName + "_OPT", StringComparison.Ordinal);
                string macro = isOpt ? (baseName + "_OPT") : baseName;

                int p = posSym + macro.Length;
                // skip whitespace
                while (p < text.Length && char.IsWhiteSpace(text[p])) p++;
                if (p >= text.Length || text[p] != '(')
                {
                    i = posSym + 1;
                    continue;
                }
                p++; // after '('

                // parse args until matching ')'
                int paren = 1;
                int angle = 0;
                bool inStr = false;
                bool inChr = false;
                bool esc = false;
                int start = p;
                while (p < text.Length)
                {
                    char ch = text[p];
                    if (inStr)
                    {
                        if (esc) esc = false;
                        else if (ch == '\\') esc = true;
                        else if (ch == '"') inStr = false;
                        p++;
                        continue;
                    }
                    if (inChr)
                    {
                        if (esc) esc = false;
                        else if (ch == '\\') esc = true;
                        else if (ch == '\'') inChr = false;
                        p++;
                        continue;
                    }

                    if (ch == '"') { inStr = true; p++; continue; }
                    if (ch == '\'') { inChr = true; p++; continue; }

                    if (ch == '(') paren++;
                    else if (ch == ')')
                    {
                        paren--;
                        if (paren == 0)
                        {
                            var inside = text.Substring(start, p - start);
                            var name = ExtractNameArg(inside, isOpt);
                            if (!map.ContainsKey(name))
                                map[name] = index;
                            index++;
                            p++; // consume ')'
                            break;
                        }
                    }
                    else if (ch == '<') angle++;
                    else if (ch == '>' && angle > 0) angle--;
                    p++;
                }

                i = p;
            }

            return map;
        }

        private static string ExtractNameArg(string inside, bool isOpt)
        {
            // Split top-level args by comma, respecting nesting and strings
            List<string> args = new();
            var cur = new StringBuilder();
            int paren = 0;
            int angle = 0;
            bool inStr = false;
            bool inChr = false;
            bool esc = false;

            foreach (char ch in inside)
            {
                if (inStr)
                {
                    cur.Append(ch);
                    if (esc) esc = false;
                    else if (ch == '\\') esc = true;
                    else if (ch == '"') inStr = false;
                    continue;
                }
                if (inChr)
                {
                    cur.Append(ch);
                    if (esc) esc = false;
                    else if (ch == '\\') esc = true;
                    else if (ch == '\'') inChr = false;
                    continue;
                }

                if (ch == '"') { inStr = true; cur.Append(ch); continue; }
                if (ch == '\'') { inChr = true; cur.Append(ch); continue; }

                if (ch == '(') paren++;
                else if (ch == ')') { if (paren > 0) paren--; }
                else if (ch == '<') angle++;
                else if (ch == '>' && angle > 0) angle--;

                if (ch == ',' && paren == 0 && angle == 0)
                {
                    args.Add(cur.ToString().Trim());
                    cur.Clear();
                }
                else
                {
                    cur.Append(ch);
                }
            }
            var tail = cur.ToString().Trim();
            if (tail.Length > 0) args.Add(tail);

            // ESPB_SYM("name", expr) => args[0]
            // ESPB_SYM_OPT(cfg, "name", expr) => args[1]
            int nameIdx = isOpt ? 1 : 0;
            if (args.Count <= nameIdx) throw new InvalidDataException("Bad ESPB_SYM line: " + inside);
            return ParseCString(args[nameIdx]);
        }

        private static string ParseCString(string s)
        {
            s = s.Trim();
            if (s.Length < 2 || s[0] != '"' || s[^1] != '"')
                throw new InvalidDataException("Expected string literal, got: " + s);
            // minimal unescape
            var body = s.Substring(1, s.Length - 2);
            return System.Text.RegularExpressions.Regex.Unescape(body);
        }
    }
}
