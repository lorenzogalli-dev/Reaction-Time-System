// Tutto il codice sta in app.cpp, incluse setup() e loop().
//
// Perche': il preprocessore che Arduino applica ai file .ino genera da solo i
// prototipi delle funzioni usando ctags, e su questo Mac ctags e' un binario
// x86 che senza Rosetta non parte. Sostituito con universal-ctags (arm64) i
// prototipi vengono generati male. Un .cpp viene compilato cosi' com'e', senza
// quel passaggio, quindi il problema non si pone.
