// Liste les fonctions d'une plage RVA qui lisent les champs du masque (déplacements 0x20, 0x24 et 0x28).
// Args: <outfile> <rvaStart> <rvaEnd> [maxDecomp]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.io.*;
import java.util.*;

public class FindMask extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        PrintWriter out = new PrintWriter(new FileWriter(a[0]));
        long base = currentProgram.getImageBase().getOffset();
        long start = Long.parseLong(a[1].replace("0x", ""), 16);
        long end = Long.parseLong(a[2].replace("0x", ""), 16);
        int max = a.length > 3 ? Integer.parseInt(a[3]) : 6;
        FunctionIterator it = currentProgram.getFunctionManager().getFunctions(toAddr(base + start), true);
        List<Function> hits = new ArrayList<>();
        while (it.hasNext()) {
            Function f = it.next();
            long rva = f.getEntryPoint().getOffset() - base;
            if (rva > end) break;
            boolean d20 = false, d24 = false, d28 = false;
            InstructionIterator insIt = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (insIt.hasNext()) {
                String s = insIt.next().toString();
                if (s.contains("+0x20]") || s.contains("+0x20,")) d20 = true;
                if (s.contains("+0x24]") || s.contains("+0x24,")) d24 = true;
                if (s.contains("+0x28]") || s.contains("+0x28,")) d28 = true;
            }
            if (d20 && d24 && d28) {
                out.println("HIT " + f.getName() + " rva=0x" + Long.toHexString(rva)
                        + " size=" + f.getBody().getNumAddresses());
                hits.add(f);
            }
        }
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        int n = 0;
        for (Function f : hits) {
            if (n++ >= max) break;
            DecompileResults r = d.decompileFunction(f, 90, monitor);
            out.println("\n===== " + f.getName() + " rva=0x" + Long.toHexString(f.getEntryPoint().getOffset() - base));
            String c = r.decompileCompleted() ? r.getDecompiledFunction().getC() : "decompile failed";
            if (c.length() > 12000) c = c.substring(0, 12000) + "\n...truncated...";
            out.println(c);
        }
        out.close();
    }
}
