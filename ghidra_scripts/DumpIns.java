// Affiche les instructions d'une fonction. Args: <outfile> <rva>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;

public class DumpIns extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        PrintWriter out = new PrintWriter(new FileWriter(a[0]));
        long base = currentProgram.getImageBase().getOffset();
        long rva = Long.parseLong(a[1].replace("0x", ""), 16);
        int count = a.length > 2 ? Integer.parseInt(a[2]) : 40;
        ghidra.program.model.address.Address addr = toAddr(base + rva);
        Function f = getFunctionContaining(addr);
        out.println((f == null ? "?" : f.getName()) + " start rva=0x" + Long.toHexString(rva));
        Instruction ins = currentProgram.getListing().getInstructionAt(addr);
        if (ins == null) ins = currentProgram.getListing().getInstructionContaining(addr);
        int n = 0;
        while (ins != null && n < count) {
            out.println(ins.getAddress() + "  " + ins);
            ins = ins.getNext();
            n++;
        }
        out.close();
    }
}
