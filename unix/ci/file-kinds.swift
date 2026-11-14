// Checks the names Finder gives the files Notepad++ opens (W37): run as `xcrun swift file-kinds.swift <Notepad++.app>`.
//
// Finder's Kind for a file comes from the app that opens its type (Apple's Launch Services: a document type's
// CFBundleTypeName is its "kind string"), else from the type's own description. macOS gives plain text no name of its
// own (only "text") and does not know many of the languages' extensions at all. So the app's Info.plist must name plain
// text as TextEdit does, and declare, as imported types, each language with upstream's long name for it, as text. Where
// macOS has a type of its own for an extension, macOS's stays.
import CoreServices
import Foundation
import UniformTypeIdentifiers

var failures = 0
func check(_ ok: Bool, _ what: @autoclosure () -> String) {
    if !ok {
        print("FAIL: \(what())")
        failures += 1
    }
}

let app = URL(fileURLWithPath: CommandLine.arguments[1])
let infoURL = app.appendingPathComponent("Contents/Info.plist")
guard let data = try? Data(contentsOf: infoURL),
    let info = try? PropertyListSerialization.propertyList(from: data, format: nil) as? [String: Any]
else {
    print("FAIL: no Info.plist at \(infoURL.path)")
    exit(1)
}
let bundleID = info["CFBundleIdentifier"] as? String ?? ""
let documents = info["CFBundleDocumentTypes"] as? [[String: Any]] ?? []
let imported = info["UTImportedTypeDeclarations"] as? [[String: Any]] ?? []

func claims(_ type: String) -> [[String: Any]] {
    documents.filter { ($0["LSItemContentTypes"] as? [String] ?? []).contains(type) }
}

// Plain text, named as TextEdit names it.
check(
    claims("public.plain-text").contains { $0["CFBundleTypeName"] as? String == "Plain Text Document" },
    "no document type names public.plain-text \"Plain Text Document\"")

// Each language: an imported type of the app's own, named, text, with its extensions, and claimed under that name.
check(!imported.isEmpty, "no imported types")
for type in imported {
    let id = type["UTTypeIdentifier"] as? String ?? ""
    let name = type["UTTypeDescription"] as? String ?? ""
    let conforms = type["UTTypeConformsTo"] as? [String] ?? []
    let tags = type["UTTypeTagSpecification"] as? [String: Any] ?? [:]
    let extensions = tags["public.filename-extension"] as? [String] ?? []
    check(id.hasPrefix(bundleID + ".language."), "\(id): not one of the app's own")
    check(!name.isEmpty, "\(id): no name")
    check(conforms.contains("public.plain-text"), "\(id): not declared as text")
    check(!extensions.isEmpty, "\(id): no extensions")
    check(claims(id).contains { $0["CFBundleTypeName"] as? String == name }, "\(id): not claimed as \"\(name)\"")
}
let batch = imported.first { $0["UTTypeIdentifier"] as? String == bundleID + ".language.batch" }
check(batch?["UTTypeDescription"] as? String == "Batch file", "batch: not named \"Batch file\"")

// What macOS makes of it, once the app is registered: an extension macOS does not know is the language's; one it
// knows stays macOS's own.
LSRegisterURL(app as CFURL, true)
let bat = UTType(filenameExtension: "bat")
check(bat?.identifier == bundleID + ".language.batch", ".bat is \(bat?.identifier ?? "-"), not the batch language")
check(bat?.localizedDescription == "Batch file", ".bat is called \(bat?.localizedDescription ?? "-")")
check(bat?.conforms(to: .plainText) == true, ".bat is not text")
let c = UTType(filenameExtension: "c")
check(c?.identifier == "public.c-source", ".c is \(c?.identifier ?? "-"), not macOS's own C source")

if failures > 0 {
    print("\(failures) failed")
    exit(1)
}
print("file kinds: \(imported.count) languages declared; plain text named; .bat a Batch file; .c macOS's own")
