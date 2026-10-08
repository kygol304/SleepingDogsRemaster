// Cherche une constante dans les instructions et décompile les fonctions qui l'utilisent.
// Args: <hexconst> <outfile> [maxfuncs]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import java.io.*;
import java.util.*;

public class FindConst extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        long target = Long.parseUnsignedLong(a[0].replace("0x",""), 16);
        int max = a.length > 2 ? Integer.parseInt(a[2]) : 20;
        PrintWriter out = new PrintWriter(new FileWriter(a[1]));
        long base = currentProgram.getImageBase().getOffset();
        Set<Function> funcs = new LinkedHashSet<>();
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext() && !monitor.isCancelled()) {
            Instruction ins = it.next();
            for (int i = 0; i < ins.getNumOperands(); i++)
                for (Object o : ins.getOpObjects(i))
                    if (o instanceof Scalar && (((Scalar)o).getUnsignedValue() & 0xffffffffL) == target) {
                        Function f = getFunctionContaining(ins.getAddress());
                        out.println("HIT " + ins.getAddress() + " rva=0x" + Long.toHexString(ins.getAddress().getOffset()-base) + "  " + ins + "  in " + (f==null?"?":f.getName()+" @0x"+Long.toHexString(f.getEntryPoint().getOffset()-base)));
                        if (f != null) funcs.add(f);
                    }
        }
        DecompInterface d = new DecompInterface(); d.openProgram(currentProgram);
        int n = 0;
        for (Function f : funcs) {
            if (n++ >= max) break;
            DecompileResults r = d.decompileFunction(f, 60, monitor);
            out.println("\n===== " + f.getName() + " rva=0x" + Long.toHexString(f.getEntryPoint().getOffset()-base));
            out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "decompile failed");
        }
        out.close();
    }
}
