// Liste les références vers une RVA. Args: <outfile> <rva>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class Xrefs extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        PrintWriter out = new PrintWriter(new FileWriter(a[0]));
        long base = currentProgram.getImageBase().getOffset();
        long rva = Long.parseLong(a[1].replace("0x", ""), 16);
        ReferenceIterator it = currentProgram.getReferenceManager().getReferencesTo(toAddr(base + rva));
        int n = 0;
        while (it.hasNext()) {
            Reference r = it.next();
            Function f = getFunctionContaining(r.getFromAddress());
            out.println(r.getReferenceType() + " from " + r.getFromAddress()
                    + " rva=0x" + Long.toHexString(r.getFromAddress().getOffset() - base)
                    + " in " + (f == null ? "?" : f.getName() + " @0x" + Long.toHexString(f.getEntryPoint().getOffset() - base)));
            n++;
        }
        out.println("count " + n);
        out.close();
    }
}
