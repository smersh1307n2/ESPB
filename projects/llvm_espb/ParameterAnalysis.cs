using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using LLVMSharp.Interop;
using System.Runtime.InteropServices;

namespace ESPB_Runtime
{
    public class ParameterAnalysis
    {
        private readonly TextWriter _logger;
        private readonly SpecialFunctionAnalyzer _specialFunctionAnalyzer;
        private readonly AdvancedParameterAnalysis _advancedParameterAnalysis;

        public ParameterAnalysis(TextWriter logger)
        {
            _logger = logger;
            _specialFunctionAnalyzer = new SpecialFunctionAnalyzer(logger);
            _advancedParameterAnalysis = new AdvancedParameterAnalysis(logger);
        }

        public unsafe MarshallingInfo AnalyzeArgumentForMarshalling(LLVMValueRef function, uint paramIndex, string functionName)
        {
            var marshalling = _advancedParameterAnalysis.AnalyzeParameterAdvanced(function, paramIndex, functionName);
            
            if (marshalling.Direction != MarshallingDirection.None)
            {
                LLVMValueRef parameter = LLVM.GetParam(function, paramIndex);
                this.DetermineBufferSize(marshalling, parameter, paramIndex, functionName);
            }
            
            if (marshalling.Direction == MarshallingDirection.Out || marshalling.Direction == MarshallingDirection.InOut)
            {
                _logger.WriteLine($"      🎯 Analyzing handler type for {marshalling.Direction} parameter...");
                
                LLVMValueRef callInstruction = default;
                
                byte handlerIndex = _specialFunctionAnalyzer.GetHandlerIndex(function, paramIndex, callInstruction);
                marshalling.HandlerIndex = handlerIndex;
                
                _logger.WriteLine($"      ✅ Handler determined: {handlerIndex} (0=standard, 1=async)");
            }
            
            return marshalling;
        }
        
        private unsafe MarshallingDirection AnalyzeParameterAttributes(LLVMValueRef function, uint paramIndex)
        {
            try
            {
                _logger.WriteLine($"      LLVM attributes analysis not implemented yet");
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"      Error analyzing attributes: {ex.Message}");
            }
            
            return MarshallingDirection.None;
        }
        
        private unsafe MarshallingDirection AnalyzeParameterUsage(LLVMValueRef parameter, string functionName, uint paramIndex)
        {
            try
            {
                bool hasLoad = false;
                bool hasStore = false;
                int usageCount = 0;
                
                LLVMUseRef useRef = LLVM.GetFirstUse(parameter);
                while (!useRef.Equals(default(LLVMUseRef)))
                {
                    usageCount++;
                    LLVMValueRef user = LLVM.GetUser(useRef);
                    
                    if (LLVM.GetValueKind(user) == LLVMValueKind.LLVMInstructionValueKind)
                    {
                        LLVMOpcode opcode = LLVM.GetInstructionOpcode(user);
                        
                        switch (opcode)
                        {
                            case LLVMOpcode.LLVMLoad:
                                hasLoad = true;
                                _logger.WriteLine($"      Found LOAD operation - indicates reading");
                                break;
                                
                            case LLVMOpcode.LLVMStore:
                                LLVMValueRef storeAddr = LLVM.GetOperand(user, 1);
                                if (storeAddr.Equals(parameter))
                                {
                                    hasStore = true;
                                    _logger.WriteLine($"      Found STORE operation - indicates writing");
                                }
                                break;
                                
                            case LLVMOpcode.LLVMCall:
                                _logger.WriteLine($"      Parameter passed to another function call");
                                break;
                                
                            case LLVMOpcode.LLVMGetElementPtr:
                                _logger.WriteLine($"      Found GEP operation - accessing structure/array elements");
                                break;
                        }
                    }
                    
                    useRef = LLVM.GetNextUse(useRef);
                }
                
                _logger.WriteLine($"      Usage analysis: {usageCount} uses, hasLoad={hasLoad}, hasStore={hasStore}");
                
                if (hasLoad && hasStore)
                {
                    return MarshallingDirection.InOut;
                }
                else if (hasLoad && !hasStore)
                {
                    return MarshallingDirection.In;
                }
                else if (!hasLoad && hasStore)
                {
                    return MarshallingDirection.Out;
                }
                
            }
            catch (Exception ex)
            {
                _logger.WriteLine($"      Error analyzing parameter usage: {ex.Message}");
            }
            
            return MarshallingDirection.None;
        }
        
        private MarshallingDirection AnalyzeByFunctionName(string functionName, uint paramIndex, string paramName)
        {
            _logger.WriteLine($"      Analyzing by function name '{functionName}' and parameter name '{paramName}'");
            
            if (!string.IsNullOrEmpty(paramName))
            {
                string lowerParamName = paramName.ToLower();
                
                if (lowerParamName.Contains("out") || lowerParamName.Contains("result") || 
                    lowerParamName.Contains("buffer") || lowerParamName.Contains("dest"))
                {
                    _logger.WriteLine($"      Parameter name suggests OUT direction");
                    return MarshallingDirection.Out;
                }
                
                if (lowerParamName.Contains("in") || lowerParamName.Contains("src") || 
                    lowerParamName.Contains("input"))
                {
                    _logger.WriteLine($"      Parameter name suggests IN direction");
                    return MarshallingDirection.In;
                }
            }
            
            string lowerFuncName = functionName.ToLower();
            
            if ((lowerFuncName.Contains("write") || lowerFuncName.Contains("read") || 
                 lowerFuncName.Contains("get") || lowerFuncName.Contains("recv")) && paramIndex == 0)
            {
                _logger.WriteLine($"      Function name suggests first parameter is OUT");
                return MarshallingDirection.Out;
            }
            
            if ((lowerFuncName.Contains("send") || lowerFuncName.Contains("put") || 
                 lowerFuncName.Contains("set") || lowerFuncName.Contains("copy")) && paramIndex == 0)
            {
                _logger.WriteLine($"      Function name suggests first parameter is IN");
                return MarshallingDirection.In;
            }
            
            _logger.WriteLine($"      No heuristic pattern matched");
            return MarshallingDirection.None;
        }
        
        public unsafe void DetermineBufferSize(MarshallingInfo marshalling, LLVMValueRef parameter, uint paramIndex, string functionName)
        {
            // Special handling for string functions with dynamic size
            switch (functionName)
            {
                case "strcpy":
                    // strcpy(dest, src) - size = strlen(src) + 1
                    // size_kind=2 (NullTerminated), size_value=1 (index of src argument)
                    marshalling.SizeMode = SizeKind.NullTerminated;
                    marshalling.SizeValue = 1; // Index of src argument
                    _logger.WriteLine($"      Buffer size: NullTerminated from arg[1] (strlen(src)+1)");
                    return;
                    
                case "strncpy":
                    // strncpy(dest, src, n) - size = n (arg[2])
                    // size_kind=1 (FromArgument), size_value=2 (index of n argument)
                    marshalling.SizeMode = SizeKind.FromArgument;
                    marshalling.SizeValue = 2; // Index of n argument
                    _logger.WriteLine($"      Buffer size: FromArgument arg[2] (n)");
                    return;
                    
                case "memcpy":
                    // memcpy(dest, src, n) - size = n (arg[2])
                    // size_kind=1 (FromArgument), size_value=2 (index of n argument)
                    marshalling.SizeMode = SizeKind.FromArgument;
                    marshalling.SizeValue = 2; // Index of n argument
                    _logger.WriteLine($"      Buffer size: FromArgument arg[2] (n)");
                    return;
                    
                case "snprintf":
                    // snprintf(dest, size, ...) - size = arg[1]
                    marshalling.SizeMode = SizeKind.FromArgument;
                    marshalling.SizeValue = 1; // Index of size argument
                    _logger.WriteLine($"      Buffer size: FromArgument arg[1] (size)");
                    return;
                    
                default:
                    // Fallback: constant 32 bytes
                    marshalling.SizeMode = SizeKind.Constant;
                    marshalling.SizeValue = 32;
                    _logger.WriteLine($"      Buffer size: {marshalling.SizeValue} bytes (mode: {marshalling.SizeMode})");
                    return;
            }
        }
    }
}