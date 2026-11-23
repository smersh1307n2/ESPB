using System;
using System.Collections.Generic;

namespace ESPB_Runtime
{
    /// <summary>
    /// Emits ESPB bytecode and records exact positions of virtual-register operands.
    /// Used for safe post-compaction patching without parsing opcodes.
    /// </summary>
    public sealed class BytecodeEmitter
    {
        public readonly List<byte> Bytes = new();
        private readonly List<(int pos, byte reg)> _regPatches = new();

        public void AddByte(byte b) => Bytes.Add(b);
        public void AddRange(IEnumerable<byte> data) => Bytes.AddRange(data);

        public void AddReg(byte vreg)
        {
            int pos = Bytes.Count;
            Bytes.Add(vreg);
            _regPatches.Add((pos, vreg));
        }

        public void ApplyRegisterMap(Func<byte, byte> map)
        {
            int logged = 0;
            for (int i = 0; i < _regPatches.Count; i++)
            {
                var (pos, oldReg) = _regPatches[i];
                byte newReg = map(oldReg);

                // Diagnostic: if we ever patch a very small value, it might indicate we accidentally
                // recorded an immediate (e.g., table index) as a register operand.
                if (logged < 20 && oldReg <= 3)
                {
                    int start = Math.Max(0, pos - 6);
                    int len = Math.Min(Bytes.Count - start, 16);
                    var ctx = BitConverter.ToString(Bytes.GetRange(start, len).ToArray());
                    Console.WriteLine($"[PATCH-DBG] pos={pos} old={oldReg} new={newReg} ctx={ctx}");
                    logged++;
                }

                Bytes[pos] = newReg;
            }
        }
    }
}
