using LLVMSharp.Interop;

namespace ESPB_Runtime
{
    #region Helper Classes for Allocation
    
    /// <summary>
    /// Представляет "время жизни" SSA-значения в виде интервала инструкций.
    /// </summary>
    public class LiveInterval
    {
        public LLVMValueRef Value { get; } // SSA-значение
        public int Start { get; set; }     // Индекс инструкции определения (def)
        public int End { get; set; }       // Индекс инструкции последнего использования (last use)
        
        // Результаты аллокации
        public bool IsSpilled { get; set; } = false;
        public byte AssignedRegister { get; set; }
        public int SpillSlotOffset { get; set; }

        public LiveInterval(LLVMValueRef value, int start)
        {
            Value = value;
            Start = start;
            End = start; 
        }
        
        public override string ToString() => $"[Start: {Start}, End: {End}] {(IsSpilled ? $"Spilled@{SpillSlotOffset}" : $"Reg: R{AssignedRegister}")} for {Value.Name}";
    }

    /// <summary>
    /// Базовый класс для местоположения значения после аллокации.
    /// </summary>
    public abstract class AllocationLocation { }

    public class RegisterLocation : AllocationLocation
    {
        public byte Register { get; }
        public RegisterLocation(byte register) { Register = register; }
    }

    public class SpillSlotLocation : AllocationLocation
    {
        public int Offset { get; }
        public uint Size { get; }
        public SpillSlotLocation(int offset, uint size) { Offset = offset; Size = size; }
    }

    /// <summary>
    /// Содержит полные результаты работы аллокатора для одной функции.
    /// </summary>
    public class AllocationResult
    {
        public Dictionary<LLVMValueRef, AllocationLocation> Locations { get; } = new Dictionary<LLVMValueRef, AllocationLocation>();
        public int TotalSpillSlotSize { get; set; }
        public int PeakLiveRegisters { get; set; }
        public byte MaxRegisterIndex { get; set; }
    }
    
    #endregion
}