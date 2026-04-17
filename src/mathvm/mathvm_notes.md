Rapberry Pi Pico - wbudowany FPU - lista dostępnych funkcji

Sprawdzam to w dokumentacji, bo tu łatwo pomylić Pico (RP2040) z Pico 2 (RP2350), a od tego zależy odpowiedź.

Rapberry Pi Pico 2 (RP2350) - wbudowany FPU - lista dostępnych funkcji

Mam już właściwy trop: dla Pico 2 chodzi o FPU rdzenia Cortex-M33 w RP2350. Sprawdzam jeszcze dokładnie, czy pytasz o instrukcje sprzętowe FPU, czy o funkcje matematyczne z SDK, bo to nie jest to samo.

Dla Raspberry Pi Pico 2 / RP2350 wbudowany FPU to FPU rdzenia Arm Cortex-M33. To jest sprzętowe FPU dla pojedynczej precyzji: obsługuje float 32-bit zgodnie z IEEE 754-2008 / FPv5 (Armv8-M). Ma 32 rejestry single-precision S0..S31, widoczne też jako 16 rejestrów 64-bit D0..D15, ale to nie oznacza pełnego FPU dla double; Arm opisuje tu obliczenia single-precision oraz instrukcje load/store dla double.

Sprzętowo obsługiwane operacje FPU:

    dodawanie

    odejmowanie

    mnożenie

    dzielenie

    mnożenie z akumulacją / fused multiply-add

    pierwiastek kwadratowy

    konwersje między liczbami całkowitymi / fixed-point a float

    instrukcje stałych zmiennoprzecinkowych.

Dodatkowo FPU ma tryby:

    full compliance

    flush-to-zero

    default NaN,
    oraz flagi wyjątków: inexact, underflow, overflow, divide-by-zero, input denormal, invalid operation.

Jeżeli przez „lista funkcji” masz na myśli gotowe funkcje z Pico SDK, to na Arm/RP2350 zoptymalizowane są m.in.:

    sqrtf, cosf, sinf, tanf, atan2f, expf, logf

    ldexpf, copysignf, truncf, floorf, ceilf, roundf

    asinf, acosf, atanf

    sinhf, coshf, tanhf, asinhf, acoshf, atanhf

    exp2f, log2f, exp10f, log10f

    powf, hypotf, cbrtf, fmodf, dremf, remainderf, remquof

    expm1f, log1pf, fmaf

    GNU: powintf, sincosf

    oraz konwersje typu int2float, float2int_z, fix2float, float2fix itd.

Najważniejsze praktycznie:

    float: obsługa sprzętowa przez FPU.

    double: nie przez ten FPU; RP2350 ma osobny mechanizm przyspieszania double w SDK, ale to nie jest ten sam blok co FPU Cortex-M33.

Mogę też rozpisać to w formie:

    lista instrukcji asemblerowych FPU, albo

    lista funkcji C/C++ z Pico SDK.


1. lista instrukcji asemblerowych FPU
2. lista funkcji C/C++ z Pico SDK  z opisem

Zbieram obie rzeczy z dokumentacji producenta: zestaw instrukcji FPU dla Cortex-M33 oraz listę funkcji z Pico SDK dla RP2350, żeby nie mieszać sprzętu z biblioteką.

Mam już komplet nazw z dokumentacji Arm. Dociągam jeszcze stronę z datasheetu RP2350, żeby potwierdzić ograniczenie do single-precision i nie dopisać niczego ponad to.

Dla Pico 2 / RP2350 na rdzeniu Cortex-M33 sprzętowy FPU obsługuje single-precision (float, 32 bity). W SDK obliczenia na float domyślnie korzystają z tego FPU, a na Arm/RP2350 domyślne pico_float_pico jest równoważne pico_float_pico_vfp, czyli podstawowe operacje float kompilator może emitować jako inline VFP/FPU. Rejestry FPU to S0..S31, a w asemblerze zwykle spotkasz składnię z sufiksem .F32.
1) Lista instrukcji asemblerowych FPU

Poniżej jest praktyczna lista mnemoników / rodzin instrukcji dostępnych dla FPU Cortex-M33 w RP2350.

    VABS, VNEG — wartość bezwzględna i zmiana znaku liczby zmiennoprzecinkowej.

    VADD, VSUB, VMUL, VDIV, VSQRT — podstawowa arytmetyka: dodawanie, odejmowanie, mnożenie, dzielenie i pierwiastek.

    VMLA, VMLS, VNMLA, VNMLS, VNMUL, VFMA, VFMS — warianty mnożenia z akumulacją, odejmowaniem i formami „negated”; VFMA/VFMS to fused multiply-add/subtract.

    VCMP, VCMPE — porównanie dwóch rejestrów FP albo rejestru z zerem; wynik trafia do flag FPSCR.

    VSEL, VMAXNM, VMINNM — selekcja wyniku na podstawie flag oraz „numeric max/min” z obsługą NaN zgodną z FPv5.

    VCVT, VCVTR — konwersje między float a typami całkowitymi oraz konwersje float/fixed-point; VCVTR używa trybu zaokrąglania z FPSCR.

    VCVTA, VCVTM, VCVTN, VCVTP — konwersje z wymuszonym kierunkiem zaokrąglenia: odpowiednio nearest-away, minus infinity, nearest, plus infinity.

    VRINTA, VRINTM, VRINTN, VRINTP, VRINTR, VRINTX, VRINTZ — zaokrąglanie liczby FP do wartości całkowitej pozostającej w formacie FP, z różnymi regułami zaokrąglania.

    VCVTB, VCVTT — konwersje między half-precision i single-precision bez pośredniego zaokrąglania.

    VMOV — przenoszenie danych: FP↔FP, immediate→FP, core register↔FP scalar.

    VMRS, VMSR — odczyt / zapis rejestrów statusu i sterowania FPU, głównie FPSCR.

    VLDR, VSTR, VLDM, VSTM, VPUSH, VPOP — ładowanie i zapis rejestrów FP z pamięci oraz na stos.

    FLDMDBX, FLDMIAX, FSTMDBX, FSTMIAX — specjalne blokowe transfery FP, wymienione w dokumentacji M33 jako osobne instrukcje; to raczej instrukcje systemowe / kontekstowe niż coś, co zwykle pisze się ręcznie w zwykłym kodzie.

W praktyce, do ręcznego assemblera na Pico 2 najczęściej używa się tylko: VADD/VSUB/VMUL/VDIV/VSQRT, VCMP, VMOV, VLDR/VSTR, VPUSH/VPOP i czasem VFMA.
2) Lista funkcji C/C++ z Pico SDK z opisem

To nie jest osobny „język FPU”, tylko zestaw zoptymalizowanych podmian w bibliotece pico_float dla Arm/RP2350. Dla _vfp podstawowe operatory float zostają kompilatorowi, a SDK dostarcza zoptymalizowane funkcje trygonometryczne i naukowe; istnieje też wariant _dcp, który może działać bez włączania CPU FPU.
Standardowe funkcje matematyczne float

    sqrtf — pierwiastek kwadratowy.
    cosf, sinf, tanf — funkcje trygonometryczne.
    atan2f — kąt z dwóch składowych y, x.
    expf, logf — wykładnicza naturalna i logarytm naturalny.

    ldexpf — mnoży liczbę przez 2^n.
    copysignf — bierze wartość z pierwszego argumentu i znak z drugiego.
    truncf, floorf, ceilf, roundf — różne tryby zaokrąglania do liczby całkowitej.

    asinf, acosf, atanf — funkcje odwrotne trygonometryczne.
    sinhf, coshf, tanhf — funkcje hiperboliczne.
    asinhf, acoshf, atanhf — funkcje odwrotne hiperboliczne.

    exp2f, log2f — wykładnicza i logarytm o podstawie 2.
    exp10f, log10f — wykładnicza i logarytm o podstawie 10.

    powf — potęgowanie x^y.
    hypotf — długość przeciwprostokątnej / sqrt(x*x + y*y) z lepszą numeryką.
    cbrtf — pierwiastek sześcienny.

    fmodf, dremf, remainderf, remquof — warianty reszty z dzielenia dla float; remquof dodatkowo zwraca część informacji o ilorazie.

    expm1f — liczy exp(x) - 1 stabilniej dla małych x.
    log1pf — liczy log(1 + x) stabilniej dla małych x.
    fmaf — fused multiply-add, czyli a*b + c z jednym krokiem zaokrąglenia.

    powintf — GNU extension: potęgowanie z wykładnikiem całkowitym.
    sincosf — liczy sinus i cosinus jednym wywołaniem.

Konwersje int / float

    int2float, uint2float, int642float, uint642float — konwersja z typów całkowitych do float, z zaokrągleniem do najbliższej wartości.

    float2int_z, float2uint_z, float2int64_z, float2uint64_z — konwersja float → integer, z obcięciem w kierunku zera.

    float2int, float2uint, float2int64, float2uint64 — konwersja float → integer, z zaokrągleniem w kierunku -∞.

Konwersje fixed-point / float

    fix2float, ufix2float, fix642float, ufix642float — konwersja liczb stałoprzecinkowych do float, z zaokrągleniem do najbliższej wartości.

    float2fix_z, float2ufix_z, float2fix64_z, float2ufix64_z — float → fixed-point, z obcięciem do zera.

    float2fix, float2ufix, float2fix64, float2ufix64 — float → fixed-point, z zaokrągleniem w dół.

Szybsze, ale przybliżone

    fdiv_fast, sqrtf_fast — szybsze wersje dzielenia i pierwiastka, ale dokumentacja zaznacza, że nie są poprawnie zaokrąglane; dotyczą wariantu pico_float_pico_dcp.

Rzeczy, które SDK też podmienia, ale zwykle nie wywołuje się ich ręcznie

    __aeabi_fadd, __aeabi_fdiv, __aeabi_fmul, __aeabi_frsub, __aeabi_fsub, a także część porównań i konwersji __aeabi_* — to wewnętrzne helpery ABI, których używa kompilator / runtime. Dobrze o nich wiedzieć przy analizie symboli w ELF, ale to nie jest typowe API aplikacyjne.

Jeżeli potrzebujesz, mogę w następnym kroku zrobić z tego jednostronicową ściągę: „mnemonik FPU → co robi → odpowiednik w C → uwagi dla RP2350”.

ja zrealizować i czy FPU pomoże w działaniach matrycowych na matrycach 3x3

Sprawdzam jeszcze dwie rzeczy w dokumentacji: jak najlepiej włączyć sprzętowe float na RP2350 i czy warto użyć gotowych funkcji macierzowych z CMSIS-DSP zamiast pisać wszystko ręcznie.

Tak — na Pico 2 / RP2350 FPU realnie pomoże w działaniach na macierzach 3×3, ale tylko wtedy, gdy liczysz na float (single precision, 32-bit). FPU Cortex-M33 w RP2350 sprzętowo obsługuje dodawanie, odejmowanie, mnożenie, dzielenie, pierwiastek i fused multiply-add, czyli dokładnie ten typ operacji, z którego składa się mnożenie macierzy. Nie jest to pełne sprzętowe przyspieszenie dla double.

Najpraktyczniej zrealizować to tak:

    trzymaj macierz jako float[9] w porządku wierszowym,

    dla stałego rozmiaru 3×3 napisz osobną, rozwiniętą funkcję, bez pętli ogólnego przeznaczenia,

    używaj zwykłego C z optymalizacją kompilatora, zamiast pisać ręcznie ASM,

    jeśli chcesz wymusić wzorzec a*b+c, używaj fmaf(), które Pico SDK optymalizuje dla float. Pico SDK na RP2350 domyślnie używa ścieżki pico_float_pico_vfp, gdzie podstawowe operacje float kompilator może emitować jako inline kod FPU/VFP.

Najlepszy wariant dla 3×3: ręcznie rozwinięta funkcja

#include <math.h>
#include <string.h>

typedef struct {
    float v[9];   // row-major: [r0c0 r0c1 r0c2 r1c0 ... r2c2]
} mat3;

typedef struct {
    float x, y, z;
} vec3;

static inline void mat3_mul(const mat3 *restrict A,
                            const mat3 *restrict B,
                            mat3 *restrict C)
{
    const float *a = A->v;
    const float *b = B->v;
    float *c = C->v;

    // Kolumny B czytane "na sztywno"
    const float b00 = b[0], b01 = b[1], b02 = b[2];
    const float b10 = b[3], b11 = b[4], b12 = b[5];
    const float b20 = b[6], b21 = b[7], b22 = b[8];

    // Wiersz 0
    c[0] = fmaf(a[0], b00, fmaf(a[1], b10, a[2] * b20));
    c[1] = fmaf(a[0], b01, fmaf(a[1], b11, a[2] * b21));
    c[2] = fmaf(a[0], b02, fmaf(a[1], b12, a[2] * b22));

    // Wiersz 1
    c[3] = fmaf(a[3], b00, fmaf(a[4], b10, a[5] * b20));
    c[4] = fmaf(a[3], b01, fmaf(a[4], b11, a[5] * b21));
    c[5] = fmaf(a[3], b02, fmaf(a[4], b12, a[5] * b22));

    // Wiersz 2
    c[6] = fmaf(a[6], b00, fmaf(a[7], b10, a[8] * b20));
    c[7] = fmaf(a[6], b01, fmaf(a[7], b11, a[8] * b21));
    c[8] = fmaf(a[6], b02, fmaf(a[7], b12, a[8] * b22));
}

static inline vec3 mat3_mul_vec3(const mat3 *restrict A, vec3 x)
{
    const float *a = A->v;
    vec3 y;

    y.x = fmaf(a[0], x.x, fmaf(a[1], x.y, a[2] * x.z));
    y.y = fmaf(a[3], x.x, fmaf(a[4], x.y, a[5] * x.z));
    y.z = fmaf(a[6], x.x, fmaf(a[7], x.y, a[8] * x.z));

    return y;
}

To jest zwykle lepsze niż ogólna funkcja „dla dowolnego N×M”, bo dla 3×3:

    nie ma kosztu liczenia indeksów w pętli ogólnej,

    kompilator łatwiej trzyma wartości w rejestrach,

    wzór a*b+c dobrze pasuje do FPU z fused MAC.

Ustawienia w Pico SDK

W typowym projekcie Pico SDK na RP2350 zwykle nie musisz ręcznie „włączać FPU”; ścieżka pico_float_pico na Arm/RP2350 odpowiada pico_float_pico_vfp, czyli tej korzystającej z inline kodu FPU. Możesz to też ustawić jawnie funkcją pico_set_float_implementation(<TARGET> xxx) albo globalnie przez PICO_DEFAULT_FLOAT_IMPL.

Jeżeli budujesz poza standardowym setupem Pico SDK, to w GCC:

    -mfloat-abi=hard pozwala generować instrukcje FP i używać ABI dla FPU,

    -mfloat-abi=softfp też może generować instrukcje sprzętowe, ale z soft-float calling convention,

    -ffp-contract=fast pozwala kompilatorowi sklejać a*b+c do fused multiply-add, jeśli architektura to wspiera.

Przykładowo w CMake możesz zostawić standardowy target Pico i dopilnować optymalizacji:

target_compile_options(my_app PRIVATE -O3)
# opcjonalnie, jeśli chcesz agresywniejsze łączenie a*b+c:
target_compile_options(my_app PRIVATE -ffp-contract=fast)

A co z CMSIS-DSP?

Jeśli zależy Ci na wygodzie i gotowych API, możesz użyć stylu CMSIS-DSP z arm_matrix_instance_f32. Ta struktura przechowuje numRows, numCols i wskaźnik pData, a dane są w układzie row-major pData[i*numCols + j]. Funkcje macierzowe są ogólne i domyślnie robią też sprawdzanie rozmiarów.

To oznacza, że dla bardzo małej stałej macierzy 3×3 CMSIS-DSP jest wygodne, ale często nie będzie najszybsze; z samej dokumentacji widać, że to API generyczne i ma dodatkowy narzut na strukturę oraz size-check. To już jest wniosek praktyczny z budowy tego API, nie deklaracja producenta.

Schemat użycia wygląda tak:

#include "arm_math.h"

float A_data[9] = {
    1,2,3,
    4,5,6,
    7,8,9
};

float B_data[9] = {
    9,8,7,
    6,5,4,
    3,2,1
};

float C_data[9];

arm_matrix_instance_f32 A, B, C;

arm_mat_init_f32(&A, 3, 3, A_data);
arm_mat_init_f32(&B, 3, 3, B_data);
arm_mat_init_f32(&C, 3, 3, C_data);

// arm_mat_mult_f32(&A, &B, &C);

Kiedy FPU pomoże najbardziej

Najbardziej:

    mnożenie macierz×macierz,

    macierz×wektor,

    obroty 3D, transformacje, filtry Kalmana, estymacja, trigonometria,

    kod na float.

Mniej lub wcale:

    gdy liczysz na int,

    gdy używasz double,

    gdy operacji jest bardzo mało i większy koszt daje kopiowanie danych niż samo liczenie.

Najkrótsza rekomendacja

Dla RP2350 i macierzy 3×3:

    użyj float,

    trzymaj dane w float[9],

    napisz ręcznie rozwinięte mat3_mul() i mat3_mul_vec3(),

    kompiluj z -O3,

    w miejscach a*b+c użyj fmaf().

To jest zwykle najlepszy punkt między prostotą i wydajnością na Pico 2.

Mogę też przygotować gotowy plik mat3.h / mat3.c pod Pico SDK z funkcjami: mnożenie, dodawanie, transpozycja, wyznacznik i odwrotność 3×3.

jak przywrócić INtellisense w VS Code

Jeśli chodzi o C/C++ w VS Code, to IntelliSense najczęściej wraca po przejściu tej krótkiej sekwencji: VS Code dokumentuje, że gdy IntelliSense przestaje działać, często po prostu nie działa language service, więc najpierw zrób restart VS Code. Jeśli projekt jest duży, wyklucz katalogi build/output, bo duże workspace’y potrafią częściowo wyłączyć funkcje IntelliSense dla wydajności.

Następnie sprawdź, czy masz zainstalowane i włączone rozszerzenie Microsoft C/C++. To właśnie ono dostarcza IntelliSense dla plików *.c i *.cpp w VS Code. Otwórz Extensions (Ctrl+Shift+X) i upewnij się, że rozszerzenie C/C++ jest aktywne.

Potem uruchom reset cache: Ctrl+Shift+P → C/C++: Reset IntelliSense Database. Oficjalne FAQ rozszerzenia podaje ten command właśnie do odtworzenia bazy IntelliSense.

Jeśli dalej nie działa, skonfiguruj kompilator: Ctrl+Shift+P → C/C++: Edit Configurations (UI) i ustaw Compiler path na rzeczywisty kompilator, którego używasz do buildu, oraz IntelliSense mode zgodny z targetem. Dokumentacja podaje, że compilerPath służy do wykrycia systemowych include’ów i define’ów, a intelliSenseMode emuluje właściwy kompilator/architekturę.

Dla Pico / RP2040 / RP2350 zwykle oznacza to konfigurację pod ARM cross-compile, nie pod hosta x64. Oficjalny przykład dla cross-compilingu pokazuje compilerPath w stylu arm-none-eabi-g++ oraz tryb IntelliSense typu gcc-arm dla celu Arm.

Jeżeli budujesz przez CMake, najstabilniej działa podpięcie compile_commands.json albo użycie CMake Tools jako configuration provider. VS Code oficjalnie wspiera oba podejścia; jeśli dla danego pliku nie ma wpisu w compile_commands.json, rozszerzenie wraca do bazowej konfiguracji z c_cpp_properties.json.

Jeśli pracujesz w WSL / SSH / devcontainerze, pamiętaj, że rozszerzenia nie synchronizują się automatycznie do zdalnego okna. W takim przypadku trzeba mieć rozszerzenie C/C++ zainstalowane także po stronie zdalnej.

Gdy problem zaczął się po zmianach w ustawieniach, możesz zresetować ustawienia użytkownika: Ctrl+Shift+P → Preferences: Open User Settings (JSON), a potem wyczyścić zawartość między {} i zapisać. Dokumentacja VS Code wskazuje to jako najprostszy sposób powrotu do domyślnych ustawień.

Dla Twojego przypadku z Pico najkrótsza ścieżka naprawy jest taka:

    Ctrl+Shift+X → sprawdź, czy C/C++ jest włączone.

    Ctrl+Shift+P → C/C++: Reset IntelliSense Database.

    Ctrl+Shift+P → C/C++: Edit Configurations (UI).

    Ustaw Compiler path na arm-none-eabi-g++ i tryb IntelliSense na wariant ARM/GCC.

    Jeśli używasz CMake, podepnij compile_commands.json.

Jeżeli chcesz, mogę od razu podać gotowy minimalny c_cpp_properties.json dla Raspberry Pi Pico 2 + Pico SDK.

przedstawię Ci swoją konwersację z autorem Picocomputer 6502 na temat koprocesora matematycznego, którego kod wbudowałem w RIA Picocomputera :
RIA math coprocessor — I have created pull requests in the picocomputer/rp6502, picocomputer/cc65, and picocomputer/examples repositories.
I compiled it, tested it — it works.
The results of the experiment are quite interesting.
The result is predictable and explains several important things:

Why CPU won (1.37s vs 2.22s)? (3dqubePRE.c 3dcubePREMC.c examples)

The coprocessor has a fixed overhead per call — parameter setup in RIA, writing the opcode to $FFEF, spin-wait on $FFF1, reading the result. Count how many such calls precompute_frame makes per vertex:

mth_itof × 4, mth_mulf × 8, mth_addf × 2, mth_subf × 3, mth_sinf, mth_cosf, mth_divf, mth_ftoi × 3
That is ~23 RIA calls per vertex, ~189 per frame → ~34 000 RIA round-trips in total (180 frames × 189 calls).

The integer version instead:

sin/cos → zero computation — plain table lookups
multiplications → cc65 lmul — slow, but local (registers + zero page only, no bus traffic)
The earlier result (4× faster with coprocessor) is not contradicted — that benchmark compared sin via the Bhaskara formula (2× 32-bit lmul + ldiv) against the coprocessor. Here sin is a table — cost = a few branches and a memory read.

Conclusion:

Scenario |    Coprocessor |    Integer 65C02
sin/cos/sqrt/exp |    wins — 6502 has no alternative |    not feasible directly
multiply/divide + many calls | loses (RIA overhead) | wins (local)
few calls, complex operations | wins | slow
The coprocessor makes sense when the computation cost is large relative to the RIA round-trip cost — that is, for transcendental functions and float32, not for integer multiplications that cc65 can handle on its own.

Anyway - solution works. 
Rumbledethumps — 10.04.2026, 09:19
that doesn't tell me anything. I only looked briefly and didn't see any benchmarks I could get without interrupting what I'm doing now. but I looked at what you did and there's ways to get much better performance. just not sure it's worth the ram vs having proper networking.
oh wait, (1.37s vs 2.22s) I see. got a bit overwhelmed with the wall of AI
WojciechGw — 10.04.2026, 09:22
Treat this as a proof of concept. It would be more useful if the registers existed in the 65C02 address space, but that would require hardware changes.
Rumbledethumps — 10.04.2026, 09:24
that calling mechanism is awesome for IO, but is hard to justify for one math operation. what you want is a stack machine that can work with and return vectors. there's a lot you can do in 512 bytes, which would diminish the call overhead.
don't forget matrix math, because that would be awesome for sprites.
WojciechGw — 10.04.2026, 09:27
That’s an interesting point — maybe it really is worth developing this further...
Rumbledethumps — 10.04.2026, 09:42
add conditionals and you could compile quite a lot of math into some kind of program that would run on the 512 byte xstack and return a tuple.

Z tej rozmowy wynika, że on nie odrzuca pomysłu, tylko mówi bardzo konkretnie: obecny model wywołań jest zbyt drobnoziarnisty.

Najważniejszy fragment to ten:

    “what you want is a stack machine that can work with and return vectors”
    “add conditionals and you could compile quite a lot of math into some kind of program”

To znaczy:
Co on faktycznie sugeruje

Nie:

    mth_addf(a,b)

    mth_mulf(a,b)

    mth_sinf(x)

    mth_ftoi(x)

wywoływane osobno przez RIA dziesiątki razy na klatkę.

Tylko:

    wysłać do koprocesora mały program

    uruchomić go raz

    odebrać gotowy wynik, najlepiej nie skalar, tylko wektor / tuple / macierz

Czyli zamiast modelu:

6502 → wywołaj jedną operację → wróć → wywołaj następną → wróć

przejść na model:

6502 → załaduj bytecode / dane → wykonaj cały pipeline matematyczny w koprocesorze → odbierz wynik

To dokładnie eliminuje problem, który sam zmierzyłeś:
nie koszt samej matematyki Cię zabija, tylko round-trip przez RIA dla każdej pojedynczej operacji.
Dlaczego on mówi o stack machine

Bo stack machine:

    ma bardzo mały interpreter

    dobrze nadaje się do wyrażeń matematycznych

    nie wymaga dużej liczby rejestrów

    łatwo ją zaszyć w 512 bajtach

    dobrze obsługuje operacje typu:

        push constant

        push input

        add/sub/mul/div

        sin/cos

        dot product

        matrix × vector

        zwróć 2–4 wartości naraz

To jest dużo lepsze niż API typu „jedna funkcja = jeden opcode”.
Co z tego wynika dla Twojego koprocesora

Masz teraz proof of concept scalar math RPC.
Naturalny następny krok to Math VM / xstack VM.

Najrozsądniejsza architektura wygląda tak:
1. Dane wejściowe

6502 wpisuje do RIA:

    argumenty skalarne

    wektory

    macierze

    stałe

2. Program

6502 wpisuje krótki bytecode, np.:

    PUSH_IN 0

    SIN

    PUSH_IN 1

    COS

    MUL

    PUSH_CONST 3

    ADD

    RET

3. Wykonanie

Koprocesor wykonuje wszystko lokalnie, bez round-trip po każdej operacji.
4. Wynik

Zwraca:

    1 skalar

    albo vec2/vec3/vec4

    albo nawet macierz

Dlaczego “vectors” i “matrix math” są tu kluczowe

Bo to dramatycznie poprawia stosunek:

koszt wywołania / ilość wykonanej pracy

Przykład:
Obecny model

dla transformacji jednego wierzchołka masz kilkanaście–kilkadziesiąt wywołań RIA.
Lepszy model

jedno wywołanie:

MAT3_MUL_VEC3 + perspective + screen transform

czyli:

    wejście: macierz 3×3, wektor 3D, parametry projekcji

    wyjście: x, y, z albo nawet screen_x, screen_y, visible

Wtedy overhead wywołania rozkłada się na całą operację.
Najlepsze operacje do “skompilowania” do koprocesora

Na początek nie robiłbym “uniwersalnego wszystkiego”, tylko kilka grubych klocków:
Poziom 1 — gotowe prymitywy

    VEC2_ADD

    VEC3_ADD

    VEC3_SUB

    DOT2

    DOT3

    MAT3_MUL_VEC3

    MAT3_MUL_MAT3

    ROT2

    ROT3_Z

    NORMALIZE2

    NORMALIZE3

Poziom 2 — pipeline graficzny

    TRANSFORM_VERTEX_2D

    TRANSFORM_VERTEX_3D

    PROJECT_PERSPECTIVE

    SPRITE_AFFINE_2D

    BATCH_TRANSFORM_N

Poziom 3 — mały VM

    PUSH

    LOAD

    STORE

    ADD

    SUB

    MUL

    DIV

    SIN

    COS

    SQRT

    DUP

    SWAP

    RET

To daje dwie ścieżki:

    szybkie gotowe instrukcje wysokiego poziomu

    oraz elastyczny fallback przez bytecode

Co znaczą “conditionals”

On sugeruje, że jeśli dodasz warunki, to VM przestanie być tylko kalkulatorem wyrażeń, a stanie się małym procesorem programowalnym.

Minimalnie:

    CMP

    JZ

    JNZ

    JLT

    JGT

    SELECT

    MIN

    MAX

    CLAMP

To już wystarczy do:

    clippingu

    testu widoczności

    wyboru ścieżki obliczeń

    prostych shaderopodobnych rzeczy

    ograniczeń i progów w fizyce / sprite’ach

Co bym z tego wyciągnął jako wniosek techniczny

Twoje testy dowiodły:

    scalar offload przez RIA nie opłaca się dla prostych operacji wykonywanych masowo

    koprocesor opłaca się dla:

        funkcji transcendentalnych

        float32

        operacji złożonych

        obliczeń pakietowych

    dalszy rozwój ma sens tylko jako:

        vector/matrix engine

        albo stack VM dla matematyki

Czyli dokładnie to, co zasugerował autor.
Co zrobiłbym dalej na Twoim miejscu

Bardzo praktyczna kolejność:
Etap A — bez pełnego VM

Dodaj kilka “grubych” opcode’ów:

    MAT3x3_VEC3

    MAT2x2_VEC2

    VEC3_DOT

    ROTATE_PROJECT_3D

    AFFINE2D_POINT

    AFFINE2D_SPRITE

To da natychmiastowy wzrost wydajności i będzie łatwe do benchmarkowania.
Etap B — batch mode

Jedno wywołanie przetwarza:

    8

    16

    32
    wierzchołki naraz

To może dać większy zysk niż sama optymalizacja pojedynczego wierzchołka.
Etap C — mini bytecode VM

Dopiero potem:

    operand stack

    1–2 lokalne rejestry

    RET_TUPLE

    proste skoki warunkowe

Najważniejsza rzecz strategiczna

Autor wprost powiedział, że pytanie nie brzmi:

“czy koprocesor matematyczny działa?”

bo to już pokazałeś,

tylko:

“jaki model programowania daje realny zysk przy tej magistrali i tym overheadzie?”

I jego odpowiedź brzmi:

nie scalar RPC, tylko coarse-grained math programs operating on vectors and matrices.
Jednym zdaniem

Twoje PoC udowodniło, że:

    matematyka w RIA działa,

    ale następny krok to nie “więcej funkcji typu mth_*”,

    tylko silnik wektorowo-macierzowy albo mały stosowy VM, który wykonuje cały blok obliczeń w jednym wywołaniu.

Mogę Ci teraz zaproponować konkretny projekt takiego koprocesora: format bytecode, zestaw opcode’ów, layout 512-byte xstack i przykładowe instrukcje dla mat3×vec3 oraz transformacji sprite’a.

proszę, zaproponuj konkretny projekt takiego koprocesora: format bytecode, zestaw opcode’ów, layout 512-byte xstack i przykładowe instrukcje dla mat3×vec3 oraz transformacji sprite’a

Sprawdzam jeszcze szczegóły RP6502/RIA, żeby zaproponować format zgodny z tym, co już istnieje, zamiast wymyślać abstrakcyjne API.

Poniżej proponuję konkretny projekt “MATHVM-32” dla RIA Picocomputera: mały, stosowy koprocesor matematyczny, który wykonuje cały kernel obliczeń w jednym wywołaniu OP, zamiast robić dziesiątki scalar RPC. Projekt opiera się na tym, co RIA już udostępnia: XSTACK ma 512 bajtów i działa jako top-down LIFO pod $FFEC, wywołanie OS startuje przez $FFEF, a $FFF1/$FFF2 obsługują spin-wait i BUSY. RIA ma też 64 KB XRAM z dwoma portalami i auto-increment, więc późniejszy batch mode można wygodnie oprzeć o sekwencyjny dostęp do tablic.
1. Model wykonania

Cel: jedno wywołanie RIA = jeden kompletny program matematyczny.

Zamiast:

    sin

    mul

    add

    ftoi

    div

    cos

    …

robisz:

    załaduj nagłówek

    załaduj locals/constants

    załaduj bytecode

    STA $FFEF

    JSR $FFF1

    odbierz gotowy tuple / vector / bbox / screen coords

To dokładnie adresuje problem, który sam zmierzyłeś: narzut siedzi głównie w round-trip przez RIA, nie w samej matematyce.
2. ABI wywołania przez XSTACK
2.1. Zasada

Ponieważ XSTACK jest LIFO, 6502 zapisuje ramkę w odwrotnej kolejności bajtów, tak żeby interpreter po stronie RIA mógł ją czytać w normalnej kolejności. Dokumentacja mówi wprost, że XSTACK jest 512-bajtowym, top-down stackiem i że zapis “pushuje”, a odczyt “pulluje”.
2.2. Format ramki wejściowej

struct mx_frame {
    // 16 bajtów
    uint8_t  version;      // = 1
    uint8_t  flags;        // bit0: use_xram_io, bit1: return_int16, bit2: saturate, bit3: debug
    uint8_t  prog_len;     // długość bytecode
    uint8_t  local_words;  // liczba 32-bit locals
    uint8_t  out_words;    // ile 32-bit słów zwrócić
    uint8_t  stack_limit;  // max depth, np. 24
    uint8_t  entry_pc;     // zwykle 0
    uint8_t  reserved0;

    uint16_t xram_in;      // 0xFFFF = brak
    uint16_t xram_out;     // 0xFFFF = brak
    uint16_t count;        // dla batch mode; 1 = single shot
    uint16_t reserved1;

    // dalej:
    // local_words * 4 bajty locals
    // prog_len bajtów bytecode
};

2.3. Wynik

Po zakończeniu:

    A = status (0 = OK, inne = błąd),

    X = liczba zwróconych 32-bit words,

    wynik jest wypchnięty na XSTACK w odwrotnej kolejności bajtów, tak żeby 6502 mógł go czytać linearnie.

2.4. Proponowany status

    0x00 OK

    0x01 bad_version

    0x02 bad_opcode

    0x03 stack_overflow

    0x04 stack_underflow

    0x05 bad_local_index

    0x06 bad_program

    0x07 numeric_fault

3. Wewnętrzny layout 512-byte VM

To jest wewnętrzny bufor VM po stronie RIA. Host nadal wysyła ramkę przez XSTACK, ale interpreter kopiuje ją do lokalnego bloku 512 B i pracuje już lokalnie.

0x000-0x0BF   LOCALS / CONSTS   192 B   = 48 x float32
0x0C0-0x11F   DATA STACK         96 B   = 24 x float32
0x120-0x1BF   BYTECODE          160 B
0x1C0-0x1DF   OUT TUPLE          32 B   = do 8 x float32
0x1E0-0x1EF   SCRATCH            16 B
0x1F0-0x1FF   HEADER             16 B

To daje bardzo użyteczny profil:

    48 locals wystarcza na:

        mat3 = 9

        vec3 = 3

        mat2x3 affine = 6

        sprite descriptor = 4–6

        kilka stałych

    24 sloty stosu spokojnie wystarczą dla małych programów matematycznych

    160 B bytecode wystarcza na małe kernle 3D/2D

Dlaczego tak

Nie robiłbym dużego call stacku ani podprogramów w v1. Lepiej mieć:

    dużo locals,

    umiarkowany operand stack,

    krótki bytecode,

    i grube opcode’y typu M3V3L i SPR2L.

4. Format bytecode
4.1. Ogólne zasady

    1 bajt opcode

    argumenty bezpośrednio po opcode

    liczby: little-endian

    komórka VM: 32 bity

    operacje F* interpretują komórkę jako float32

    skoki: signed rel8

4.2. Konwencja stosu

Wektor vec3(x,y,z) jest odkładany jako:

    x

    y

    z

czyli z leży na wierzchu.

Macierz mat3 jest w locals jako row-major:

m00 m01 m02
m10 m11 m12
m20 m21 m22

5. Zestaw opcode’ów
5.1. Sterowanie i dostęp do danych
Hex	Mnemonic	Operandy	Efekt
00	NOP	-	nic
01	HALT	-	zakończ bez wyniku
02	RET n	u8	zwróć n top words
10	PUSHF	f32	push imm float
11	LDS	u8 idx	push local[idx]
12	STS	u8 idx	pop -> local[idx]
13	LDV2	u8 idx	push local[idx], local[idx+1]
14	STV2	u8 idx	pop vec2 -> locals
15	LDV3	u8 idx	push 3 locals
16	STV3	u8 idx	pop vec3 -> locals
17	DUP	-	dup top
18	DROP	-	pop discard
19	SWAP	-	zamień dwa top
1A	OVER	-	skopiuj drugi element
5.2. Scalar float
Hex	Mnemonic	Stack effect
20	FADD	a b -> a+b
21	FSUB	a b -> a-b
22	FMUL	a b -> a*b
23	FDIV	a b -> a/b
24	FMADD	a b c -> a*b+c
25	FNEG	a -> -a
26	FABS	a -> abs(a)
27	FSQRT	a -> sqrt(a)
28	FSIN	a -> sin(a)
29	FCOS	a -> cos(a)
2A	FMIN	a b -> min(a,b)
2B	FMAX	a b -> max(a,b)
5.3. Vector / matrix
Hex	Mnemonic	Operandy	Znaczenie
30	V2ADD	-	vec2 + vec2
31	V2SUB	-	vec2 - vec2
32	V2DOT	-	vec2·vec2 -> scalar
33	V2SCALE	-	vec2 * scalar
34	A2P2L	u8 aBase, u8 pBase	affine 2x3 z locals × point2 z locals -> push vec2
38	V3ADD	-	vec3 + vec3
39	V3SUB	-	vec3 - vec3
3A	V3DOT	-	vec3·vec3 -> scalar
3B	V3SCALE	-	vec3 * scalar
3C	CROSS3	-	vec3 × vec3
3D	NORM3	-	normalizacja vec3
3E	M3V3L	u8 mBase, u8 vBase	mat3(locals[m..m+8]) * vec3(locals[v..v+2]) -> push vec3
3F	M3M3L	u8 aBase, u8 bBase	mat3 * mat3 -> push 9 words
5.4. Sprite / 2D
Hex	Mnemonic	Operandy	Znaczenie
48	SPR2L	u8 aBase, u8 sBase, u8 flags	transformacja sprite’a 2D
49	BBOX2	-	z 4 corner points -> xmin ymin xmax ymax
4A	ROUND2I	-	float tuple -> int-ish float / opcj. int16 jeśli flagi
5.5. Warunki i skoki
Hex	Mnemonic	Operandy	Znaczenie
60	CMPZ	-	a -> (a==0 ? 1 : 0)
61	FCMPLT	-	a b -> (a<b)
62	FCMPGT	-	a b -> (a>b)
63	JMP	rel8	bezwarunkowy skok
64	JZ	rel8	skok jeśli top == 0
65	JNZ	rel8	skok jeśli top != 0
66	SELECT	-	cond a b -> cond ? a : b
6. Semantyka “grubych” opcode’ów
6.1. M3V3L mBase, vBase

Czyta z locals:

    locals[mBase + 0..8] = mat3

    locals[vBase + 0..2] = vec3

Liczy:

rx = m00*x + m01*y + m02*z
ry = m10*x + m11*y + m12*z
rz = m20*x + m21*y + m22*z

i odkłada na stack:

    rx

    ry

    rz

To jest celowo fused macro-op, żeby nie pchać 12 scalar loadów i 9 mnożeń przez interpreter.
6.2. SPR2L aBase, sBase, flags

Czyta z locals:

Affine 2D:

aBase+0 = a
aBase+1 = b
aBase+2 = tx
aBase+3 = c
aBase+4 = d
aBase+5 = ty

Sprite descriptor:

sBase+0 = w
sBase+1 = h
sBase+2 = ax    // anchor x, np. 0.5
sBase+3 = ay    // anchor y, np. 0.5

Najpierw tworzy 4 rogi w lokalnym układzie:

p0 = (-ax*w,     -ay*h)
p1 = ((1-ax)*w,  -ay*h)
p2 = ((1-ax)*w,  (1-ay)*h)
p3 = (-ax*w,     (1-ay)*h)

Potem transformuje każdy punkt:

x' = a*x + b*y + tx
y' = c*x + d*y + ty

flags

    bit 0 = zwróć 8 words: x0 y0 x1 y1 x2 y2 x3 y3

    bit 1 = zwróć bbox: xmin ymin xmax ymax

    bit 2 = zaokrąglij każdą współrzędną funkcją roundf() przed zwrotem

    bity 3..7 = v1: muszą być 0

Reguły v1:

    dokładnie jeden z bitów 0 albo 1 musi być ustawiony

    jeśli ustawiono bit 2, wynik nadal zajmuje float32 wordy; dopiero MX_FLAG_RETURN_I16 zmienia format zwrotu całej ramki

    jeśli bity 3..7 są niezerowe, instrukcja zwraca MX_ERR_PROGRAM

Najbardziej praktyczne warianty v1:

    flags = 0x01 → pełne 4 rogi

    flags = 0x02 → od razu bbox do cullingu / dirty rect

7. Przykład 1 — mat3 × vec3
7.1. Locals

0..8   = mat3 row-major
9..11  = vec3

7.2. Program symboliczny

M3V3L  0, 9
RET    3

7.3. Bajty

3E 00 09 02 03

7.4. Co zwraca

Na wyjściu:

    word0 = rx

    word1 = ry

    word2 = rz

To jest właśnie przypadek, w którym z ~kilkunastu-kilkudziesięciu scalar calli robisz jedno wywołanie RIA.
8. Przykład 2 — transformacja sprite’a 2D
8.1. Locals

0 = a
1 = b
2 = tx
3 = c
4 = d
5 = ty

6 = w
7 = h
8 = ax
9 = ay

8.2. Program: zwróć 4 rogi

SPR2L  0, 6, 0x01
RET    8

8.3. Bajty

48 00 06 01 02 08

8.4. Zwrot

    x0 y0 x1 y1 x2 y2 x3 y3

8.5. Program: zwróć bbox

SPR2L  0, 6, 0x02
RET    4

8.6. Bajty

48 00 06 02 02 04

To jest bardzo dobre dla sprite’ów, bo 6502 dostaje od razu coś, co może wykorzystać do:

    rysowania,

    clippingu,

    testu widoczności,

    dirty rectangle,

    sortowania po depth / y.

9. Batch mode v2

Tu właśnie warto wykorzystać XRAM. RIA ma 64 KB XRAM, dwa portale (RW0/RW1) i auto-increment, a dokumentacja wprost zaznacza, że sekwencyjny dostęp dzięki temu jest szybki i praktyczny.
Propozycja

W batch mode:

    XSTACK niesie tylko:

        header

        stałe / matrix / parametry

        krótki bytecode

    xram_in wskazuje tablicę wejściową

    xram_out wskazuje tablicę wyników

    count = liczba elementów

Przykłady:

    count = N punktów vec3 → M3V3L_BATCH

    count = N sprite’ów → SPR2L_BATCH

To pozwala robić:

    1 wywołanie na całą listę sprite’ów

    1 wywołanie na cały bufor vertexów

A nie 1 wywołanie per vertex.
10. Minimalny stub po stronie 6502

Schemat wywołania:

; frame[] zawiera nagłówek + locals + bytecode
; pushujemy od końca

    LDY #FRAME_LEN-1
@push:
    LDA frame,Y
    STA $FFEC      ; XSTACK push
    DEY
    BPL @push

    LDA #OP_MATHVM ; np. przydzielony numer operacji
    STA $FFEF      ; start
    JSR $FFF1      ; wait until BUSY clears

    ; A = status, X = out_words
    ; potem czytasz XSTACK bytes wyniku

11. Co wdrożyłbym najpierw

Nie zaczynałbym od “pełnego języka”. Tylko od tego:
v1

    RET

    LDS, STS, LDV2, LDV3

    FADD, FSUB, FMUL, FDIV, FMADD

    FSIN, FCOS, FSQRT

    M3V3L

    A2P2L

    SPR2L

    JZ, JNZ, SELECT

v2

    CROSS3, NORM3, V3DOT

    M3M3L

    batch mode przez XRAM

    bbox/clipping helpers

    int16 output packing

v3

    małe pętle

    więcej conditionals

    opcjonalny CALL/RET dla mikroprogramów

12. Najważniejsza decyzja architektoniczna

Najmocniejszy wariant to hybryda:

    rdzeń jest stack VM

    ale ma kilka grubych opcode’ów domenowych:

        M3V3L

        A2P2L

        SPR2L

To daje:

    mały interpreter,

    mały bytecode,

    mały narzut,

    i bardzo dobry stosunek “wykonana praca / jedno wywołanie RIA”.

To jest dokładnie ten kierunek, który autor zasugerował: nie scalar RPC, tylko programowalny blok matematyczny operujący na wektorach i macierzach.

Mogę teraz rozpisać to jeszcze poziom niżej: pełną specyfikację binarną, gotowe enumy opcode’ów w C, oraz szkic interpretera po stronie RIA.

proszę, rozpisz to jeszcze poziom niżej: pełną specyfikację binarną, gotowe enumy opcode’ów w C, oraz szkic interpretera po stronie RIA

Doprecyzowuję to już na poziom implementacyjny: sprawdzam jeszcze adresy i semantykę RIA/XSTACK w oficjalnej dokumentacji, żeby specyfikacja binarna pasowała do istniejącego mechanizmu wywołań.

Poniżej masz pełną, spójną specyfikację binarną propozycji MATHVM-32 pod RIA. To jest projekt ABI i VM, który nakłada się na istniejący mechanizm RIA, a nie go zmienia: XSTACK jest 512-bajtowym stosem LIFO pod $FFEC, wywołanie OS startuje po zapisie do $FFEF, JSR $FFF1 robi spin-wait aż BUSY w $FFF2 opadnie, a wynik może wrócić przez rejestry A/X i dane odczytane z XSTACK. RIA ma też 64 KB XRAM dostępne przez RW0/RW1 z auto-increment, więc batch mode dobrze pasuje do tej architektury.

To, co poniżej, jest już moją konkretną propozycją warstwy VM.
1. Założenia ABI
1.1. Cel

Jedno wywołanie RIA ma wykonać:

    cały kernel matematyczny,

    na małej liczbie lokalnych danych,

    opcjonalnie na buforach w XRAM,

    i zwrócić gotowy wynik jako tuple / wektor / bbox.

1.2. Nazwa operacji OS

Przyjmuję jeden numer operacji OS:

#define RIA_OP_MATHVM 0x80

To jest tymczasowa decyzja projektowa dla MATHVM v1, żeby nie kolidować z istniejącym `mth`.
1.3. Jednostka danych VM

Podstawową komórką VM jest:

    1 word = 32 bity

    interpretowany jako:

        float32

        int32

        uint32

        lub bit pattern

W v1 zakładam, że prawie wszystkie operacje liczą na float32.
2. Format ramki binarnej na XSTACK

Ponieważ XSTACK jest LIFO, host 6502 zapisuje ramkę od końca do początku, żeby interpreter po stronie RIA odczytał ją w normalnej kolejności. To jest zgodne z dokumentowanym modelem XSTACK: zapis pushuje, odczyt pulluje.
2.1. Układ logiczny ramki

[header]
[locals...]
[bytecode...]

2.2. Nagłówek binarny

Rozmiar: 16 bajtów

#pragma pack(push,1)
typedef struct {
    uint8_t  magic;         // 'M' = 0x4D
    uint8_t  version;       // 0x01
    uint8_t  flags;         // patrz niżej
    uint8_t  hdr_size;      // = 16

    uint8_t  prog_len;      // długość bytecode w bajtach
    uint8_t  local_words;   // liczba 32-bit locals
    uint8_t  out_words;     // maks. liczba słów wyniku
    uint8_t  stack_words;   // limit stosu operandów, np. 24

    uint16_t xram_in;       // 0xFFFF = brak
    uint16_t xram_out;      // 0xFFFF = brak

    uint16_t count;         // 1 = single-shot, >1 = batch
    uint16_t reserved;      // 0
} mx_header_t;
#pragma pack(pop)

2.3. Flagi nagłówka

enum {
    MX_FLAG_USE_XRAM_IN    = 0x01,  // v1: musi być 0
    MX_FLAG_USE_XRAM_OUT   = 0x02,  // v1: musi być 0
    MX_FLAG_RETURN_I16     = 0x04,  // końcowy wynik zwracany jako 1x int16 na word
    MX_FLAG_SATURATE       = 0x08,  // wymagane przy RETURN_I16, inaczej overflow = błąd
    MX_FLAG_DEBUG          = 0x10,  // v1: musi być 0
    MX_FLAG_RESERVED5      = 0x20,  // v1: musi być 0
    MX_FLAG_RESERVED6      = 0x40,  // v1: musi być 0
    MX_FLAG_RESERVED7      = 0x80   // v1: musi być 0
};

2.3.1. Ograniczenia v1

Żeby pierwsza implementacja była mała i przewidywalna, v1 nie używa XRAM ani batch mode.

Wymagania nagłówka dla v1:

    count == 1

    xram_in == 0xFFFF

    xram_out == 0xFFFF

    (flags & (MX_FLAG_USE_XRAM_IN | MX_FLAG_USE_XRAM_OUT |
              MX_FLAG_DEBUG | MX_FLAG_RESERVED5 |
              MX_FLAG_RESERVED6 | MX_FLAG_RESERVED7)) == 0

    jeśli ustawiono MX_FLAG_RETURN_I16, to MX_FLAG_SATURATE też musi być ustawione

Każde naruszenie powyższego zwraca MX_ERR_UNSUPPORTED albo MX_ERR_HEADER.

2.4. Locals

Bezpośrednio po nagłówku:

local_words * 4 bajty

Każdy local jest little-endian 32-bit.

Przykład:

    local[0] = m00

    local[1] = m01

    ...

    local[8] = m22

    local[9] = vx

    local[10] = vy

    local[11] = vz

2.5. Bytecode

Po locals:

prog_len bajtów

Bez paddingu.
3. Kod statusu i rejestry wyniku

Po zakończeniu operacji:

    A = status

    X = liczba 32-bit words zwróconych na XSTACK

To dobrze pasuje do mechanizmu RETURN/BUSY/A/X, który RIA już wystawia w przestrzeni $FFF1..$FFF7.
3.1. Status

typedef enum {
    MX_OK              = 0x00,
    MX_ERR_MAGIC       = 0x01,
    MX_ERR_VERSION     = 0x02,
    MX_ERR_HEADER      = 0x03,
    MX_ERR_PROGRAM     = 0x04,
    MX_ERR_BAD_OPCODE  = 0x05,
    MX_ERR_STACK_OVF   = 0x06,
    MX_ERR_STACK_UDF   = 0x07,
    MX_ERR_BAD_LOCAL   = 0x08,
    MX_ERR_BAD_XRAM    = 0x09,
    MX_ERR_NUMERIC     = 0x0A,
    MX_ERR_UNSUPPORTED = 0x0B
} mx_status_t;

Polityka błędów numerycznych w v1:

    zwykłe operacje float (FADD, FMUL, FSIN, FCOS, FSQRT itd.) zachowują się jak IEEE754 float32 i nie zwracają błędu tylko dlatego, że wynik jest NaN/Inf

    MX_ERR_NUMERIC jest używany tylko wtedy, gdy instrukcja wymaga wyniku skończonego lub reprezentowalnego w formacie wyjściowym

    w v1 dotyczy to wyłącznie ścieżek pakowania / zaokrąglania do int16 oraz ewentualnych późniejszych opcode’ów ekranu

3.2. Wynik na XSTACK

Jeśli X = n, interpreter wypycha na XSTACK:

n * 4 bajty

w normalnej kolejności słów:

    word0

    word1

    ...

    word(n-1)

Każdy word jest zapisany little-endian.

W implementacji oznacza to:

    host buduje ramkę w buforze liniowym [header][locals][bytecode]

    host zapisuje ją na XSTACK od końca do początku

    RIA czyta header jako pierwsze 16 bajtów, wylicza pełny frame_len i dociąga resztę

    RIA zapisuje wynik tak, aby kolejne odczyty przez $FFEC zwracały:
    word0 byte0, word0 byte1, word0 byte2, word0 byte3, word1 ...

Host 6502 może więc traktować wynik jako liniowy bufor little-endian długości 4*n bajtów.
4. Wewnętrzny układ pamięci VM w RIA

To jest propozycja implementacyjna dla interpretera.

offset   size   przeznaczenie
------   ----   ----------------------------
0x000    192    locals[48]      (48 * 4)
0x0C0     96    stack[24]       (24 * 4)
0x120    160    program[160]
0x1C0     32    out[8]
0x1E0     16    scratch
0x1F0     16    header

Łącznie: 512 B

Czyli interpreter da się zmieścić w modelu „dużo da się zrobić w 512 bajtach”, o którym pisał autor, ale tu te 512 B traktujemy jako stan wykonania VM, nie jako storage hosta.
5. Format i semantyka bytecode
5.1. Zasady ogólne

    opcode = 1 bajt

    argumenty bezpośrednio po opcode

    liczby natychmiastowe są little-endian

    skoki używają rel8 liczonych od końca instrukcji

    stos operandów przechowuje 32-bit words

Walidacja i bezpieczeństwo v1:

    program musi zakończyć się przez RET albo HALT

    każdy odczyt argumentu instrukcji musi mieścić się w prog_len

    skok nie może wyjść poza zakres 0..prog_len-1

    interpreter ma limit 255 wykonanych instrukcji na jedno wywołanie; przekroczenie zwraca MX_ERR_PROGRAM

5.2. Konwencja wektorów
vec2

na stosie:

    x

    y ← top

vec3

na stosie:

    x

    y

    z ← top

mat3

w locals jako row-major:

m00 m01 m02
m10 m11 m12
m20 m21 m22

affine 2x3

w locals:

a b tx
c d ty

6. Pełne enumy opcode’ów w C

typedef enum {
    // 0x00..0x0F - control
    MX_NOP        = 0x00,
    MX_HALT       = 0x01,
    MX_RET        = 0x02, // arg: u8 count

    // 0x10..0x1F - load/store/stack
    MX_PUSHF      = 0x10, // arg: f32
    MX_PUSHI      = 0x11, // arg: i32
    MX_LDS        = 0x12, // arg: u8 idx
    MX_STS        = 0x13, // arg: u8 idx
    MX_LDV2       = 0x14, // arg: u8 idx
    MX_STV2       = 0x15, // arg: u8 idx
    MX_LDV3       = 0x16, // arg: u8 idx
    MX_STV3       = 0x17, // arg: u8 idx
    MX_DUP        = 0x18,
    MX_DROP       = 0x19,
    MX_SWAP       = 0x1A,
    MX_OVER       = 0x1B,

    // 0x20..0x2F - scalar float
    MX_FADD       = 0x20,
    MX_FSUB       = 0x21,
    MX_FMUL       = 0x22,
    MX_FDIV       = 0x23,
    MX_FMADD      = 0x24, // a b c -> a*b + c
    MX_FNEG       = 0x25,
    MX_FABS       = 0x26,
    MX_FSQRT      = 0x27,
    MX_FSIN       = 0x28,
    MX_FCOS       = 0x29,
    MX_FMIN       = 0x2A,
    MX_FMAX       = 0x2B,
    MX_FLOOR      = 0x2C,
    MX_FCEIL      = 0x2D,
    MX_FROUND     = 0x2E,
    MX_FTRUNC     = 0x2F,

    // 0x30..0x3F - vectors/matrices
    MX_V2ADD      = 0x30,
    MX_V2SUB      = 0x31,
    MX_V2DOT      = 0x32,
    MX_V2SCALE    = 0x33,
    MX_A2P2L      = 0x34, // arg: u8 aBase, u8 pBase
    MX_V3ADD      = 0x38,
    MX_V3SUB      = 0x39,
    MX_V3DOT      = 0x3A,
    MX_V3SCALE    = 0x3B,
    MX_CROSS3     = 0x3C,
    MX_NORM3      = 0x3D,
    MX_M3V3L      = 0x3E, // arg: u8 mBase, u8 vBase
    MX_M3M3L      = 0x3F, // arg: u8 aBase, u8 bBase

    // 0x40..0x4F - sprite/geometry
    MX_SPR2L      = 0x48, // arg: u8 aBase, u8 sBase, u8 flags
    MX_BBOX2      = 0x49, // 4 punkty -> bbox
    MX_ROUND2I    = 0x4A, // float tuple -> int-ish / int16 pack opcjonalnie

    // 0x60..0x6F - conditions/jumps
    MX_CMPZ       = 0x60, // a -> (a==0 ? 1.0f : 0.0f)
    MX_FCMPLT     = 0x61, // a b -> (a<b ? 1.0f : 0.0f)
    MX_FCMPGT     = 0x62, // a b -> (a>b ? 1.0f : 0.0f)
    MX_JMP        = 0x63, // arg: i8 rel
    MX_JZ         = 0x64, // arg: i8 rel
    MX_JNZ        = 0x65, // arg: i8 rel
    MX_SELECT     = 0x66  // cond a b -> cond ? a : b
} mx_opcode_t;

Zakres implementacji v1

Enum powyżej opisuje docelowy format bytecode, ale pierwsza implementacja wspiera tylko:

    MX_NOP
    MX_HALT
    MX_RET

    MX_PUSHF
    MX_LDS
    MX_LDV2
    MX_LDV3
    MX_DUP
    MX_DROP
    MX_SWAP

    MX_FADD
    MX_FSUB
    MX_FMUL
    MX_FDIV
    MX_FMADD
    MX_FNEG
    MX_FABS
    MX_FSQRT
    MX_FSIN
    MX_FCOS
    MX_FMIN
    MX_FMAX
    MX_FROUND
    MX_FTRUNC

    MX_A2P2L
    MX_M3V3L
    MX_SPR2L

Pozostałe opcode’y są zarezerwowane i w v1 zwracają MX_ERR_UNSUPPORTED.

7. Binarny format argumentów instrukcji
7.1. Proste instrukcje

00                NOP
01                HALT
02 nn             RET nn
18                DUP
19                DROP
1A                SWAP
1B                OVER
20                FADD
...

7.2. Natychmiastowe
PUSHF

10 xx xx xx xx

gdzie 4 bajty to IEEE754 float32, little-endian.
PUSHI

11 xx xx xx xx

LDS

12 ii

STS

13 ii

LDV2

14 ii

LDV3

16 ii

M3V3L

3E mm vv

SPR2L

48 aa ss ff

JMP/JZ/JNZ

63 rr
64 rr
65 rr

rr to signed int8_t.
8. Struktury C dla hosta i firmware
8.1. Reprezentacja słowa

typedef union {
    uint32_t u32;
    int32_t  i32;
    float    f32;
} mx_word_t;

8.2. Kontekst VM

#define MX_MAX_LOCALS 48
#define MX_MAX_STACK  24
#define MX_MAX_PROG   160
#define MX_MAX_OUT    8

typedef struct {
    mx_header_t hdr;

    mx_word_t locals[MX_MAX_LOCALS];
    mx_word_t stack[MX_MAX_STACK];
    mx_word_t out[MX_MAX_OUT];

    uint8_t program[MX_MAX_PROG];

    uint8_t pc;
    uint8_t sp;      // liczba zajętych slotów
    uint8_t outc;    // liczba zwracanych słów
    uint8_t status;
} mx_vm_t;

9. Helpery interpretera

static inline int mx_push(mx_vm_t *vm, mx_word_t v) {
    if (vm->sp >= vm->hdr.stack_words || vm->sp >= MX_MAX_STACK) {
        vm->status = MX_ERR_STACK_OVF;
        return 0;
    }
    vm->stack[vm->sp++] = v;
    return 1;
}

static inline int mx_pop(mx_vm_t *vm, mx_word_t *out) {
    if (vm->sp == 0) {
        vm->status = MX_ERR_STACK_UDF;
        return 0;
    }
    *out = vm->stack[--vm->sp];
    return 1;
}

static inline int mx_peek(mx_vm_t *vm, mx_word_t *out) {
    if (vm->sp == 0) {
        vm->status = MX_ERR_STACK_UDF;
        return 0;
    }
    *out = vm->stack[vm->sp - 1];
    return 1;
}

static inline int mx_check_local(mx_vm_t *vm, uint8_t idx, uint8_t count) {
    if ((uint16_t)idx + count > vm->hdr.local_words || idx + count > MX_MAX_LOCALS) {
        vm->status = MX_ERR_BAD_LOCAL;
        return 0;
    }
    return 1;
}

10. Pomocnicze dekodowanie little-endian

static inline uint32_t rd_u32le(const uint8_t *p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static inline int8_t rd_i8(const uint8_t *p) {
    return (int8_t)p[0];
}

11. Szkic interpretera po stronie RIA

To jest szkic na poziomie prawie-kompilowalnym.

#include <stdint.h>
#include <string.h>
#include <math.h>

static int mx_exec(mx_vm_t *vm) {
    vm->pc = 0;
    vm->sp = 0;
    vm->outc = 0;
    vm->status = MX_OK;

    while (vm->pc < vm->hdr.prog_len) {
        uint8_t op = vm->program[vm->pc++];

        switch (op) {
        case MX_NOP:
            break;

        case MX_HALT:
            return 1;

        case MX_RET: {
            uint8_t n = vm->program[vm->pc++];
            if (n > vm->sp || n > vm->hdr.out_words || n > MX_MAX_OUT) {
                vm->status = MX_ERR_PROGRAM;
                return 0;
            }
            for (uint8_t i = 0; i < n; ++i) {
                vm->out[i] = vm->stack[vm->sp - n + i];
            }
            vm->outc = n;
            return 1;
        }

        case MX_PUSHF: {
            if (vm->pc + 4 > vm->hdr.prog_len) {
                vm->status = MX_ERR_PROGRAM;
                return 0;
            }
            mx_word_t w;
            w.u32 = rd_u32le(&vm->program[vm->pc]);
            vm->pc += 4;
            if (!mx_push(vm, w)) return 0;
            break;
        }

        case MX_PUSHI: {
            if (vm->pc + 4 > vm->hdr.prog_len) {
                vm->status = MX_ERR_PROGRAM;
                return 0;
            }
            mx_word_t w;
            w.u32 = rd_u32le(&vm->program[vm->pc]);
            vm->pc += 4;
            if (!mx_push(vm, w)) return 0;
            break;
        }

        case MX_LDS: {
            uint8_t idx = vm->program[vm->pc++];
            if (!mx_check_local(vm, idx, 1)) return 0;
            if (!mx_push(vm, vm->locals[idx])) return 0;
            break;
        }

        case MX_STS: {
            uint8_t idx = vm->program[vm->pc++];
            mx_word_t v;
            if (!mx_check_local(vm, idx, 1)) return 0;
            if (!mx_pop(vm, &v)) return 0;
            vm->locals[idx] = v;
            break;
        }

        case MX_LDV2: {
            uint8_t idx = vm->program[vm->pc++];
            if (!mx_check_local(vm, idx, 2)) return 0;
            if (!mx_push(vm, vm->locals[idx + 0])) return 0;
            if (!mx_push(vm, vm->locals[idx + 1])) return 0;
            break;
        }

        case MX_LDV3: {
            uint8_t idx = vm->program[vm->pc++];
            if (!mx_check_local(vm, idx, 3)) return 0;
            if (!mx_push(vm, vm->locals[idx + 0])) return 0;
            if (!mx_push(vm, vm->locals[idx + 1])) return 0;
            if (!mx_push(vm, vm->locals[idx + 2])) return 0;
            break;
        }

        case MX_DUP: {
            mx_word_t v;
            if (!mx_peek(vm, &v)) return 0;
            if (!mx_push(vm, v)) return 0;
            break;
        }

        case MX_DROP: {
            mx_word_t tmp;
            if (!mx_pop(vm, &tmp)) return 0;
            break;
        }

        case MX_SWAP: {
            if (vm->sp < 2) {
                vm->status = MX_ERR_STACK_UDF;
                return 0;
            }
            mx_word_t t = vm->stack[vm->sp - 1];
            vm->stack[vm->sp - 1] = vm->stack[vm->sp - 2];
            vm->stack[vm->sp - 2] = t;
            break;
        }

        case MX_OVER: {
            if (vm->sp < 2) {
                vm->status = MX_ERR_STACK_UDF;
                return 0;
            }
            if (!mx_push(vm, vm->stack[vm->sp - 2])) return 0;
            break;
        }

        case MX_FADD:
        case MX_FSUB:
        case MX_FMUL:
        case MX_FDIV:
        case MX_FMIN:
        case MX_FMAX: {
            mx_word_t b, a, r;
            if (!mx_pop(vm, &b) || !mx_pop(vm, &a)) return 0;

            switch (op) {
            case MX_FADD: r.f32 = a.f32 + b.f32; break;
            case MX_FSUB: r.f32 = a.f32 - b.f32; break;
            case MX_FMUL: r.f32 = a.f32 * b.f32; break;
            case MX_FDIV: r.f32 = a.f32 / b.f32; break;
            case MX_FMIN: r.f32 = fminf(a.f32, b.f32); break;
            default:      r.f32 = fmaxf(a.f32, b.f32); break;
            }

            if (!mx_push(vm, r)) return 0;
            break;
        }

        case MX_FMADD: {
            mx_word_t c, b, a, r;
            if (!mx_pop(vm, &c) || !mx_pop(vm, &b) || !mx_pop(vm, &a)) return 0;
            r.f32 = fmaf(a.f32, b.f32, c.f32);
            if (!mx_push(vm, r)) return 0;
            break;
        }

        case MX_FNEG:
        case MX_FABS:
        case MX_FSQRT:
        case MX_FSIN:
        case MX_FCOS:
        case MX_FLOOR:
        case MX_FCEIL:
        case MX_FROUND:
        case MX_FTRUNC: {
            mx_word_t a, r;
            if (!mx_pop(vm, &a)) return 0;

            switch (op) {
            case MX_FNEG:   r.f32 = -a.f32; break;
            case MX_FABS:   r.f32 = fabsf(a.f32); break;
            case MX_FSQRT:  r.f32 = sqrtf(a.f32); break;
            case MX_FSIN:   r.f32 = sinf(a.f32); break;
            case MX_FCOS:   r.f32 = cosf(a.f32); break;
            case MX_FLOOR:  r.f32 = floorf(a.f32); break;
            case MX_FCEIL:  r.f32 = ceilf(a.f32); break;
            case MX_FROUND: r.f32 = roundf(a.f32); break;
            default:        r.f32 = truncf(a.f32); break;
            }

            if (!mx_push(vm, r)) return 0;
            break;
        }

        case MX_V3DOT: {
            // stack: ax ay az bx by bz
            if (vm->sp < 6) {
                vm->status = MX_ERR_STACK_UDF;
                return 0;
            }
            float bz = vm->stack[--vm->sp].f32;
            float by = vm->stack[--vm->sp].f32;
            float bx = vm->stack[--vm->sp].f32;
            float az = vm->stack[--vm->sp].f32;
            float ay = vm->stack[--vm->sp].f32;
            float ax = vm->stack[--vm->sp].f32;

            mx_word_t r;
            r.f32 = fmaf(ax, bx, fmaf(ay, by, az * bz));
            if (!mx_push(vm, r)) return 0;
            break;
        }

        case MX_M3V3L: {
            uint8_t mb = vm->program[vm->pc++];
            uint8_t vb = vm->program[vm->pc++];
            if (!mx_check_local(vm, mb, 9) || !mx_check_local(vm, vb, 3)) return 0;

            float m00 = vm->locals[mb + 0].f32;
            float m01 = vm->locals[mb + 1].f32;
            float m02 = vm->locals[mb + 2].f32;
            float m10 = vm->locals[mb + 3].f32;
            float m11 = vm->locals[mb + 4].f32;
            float m12 = vm->locals[mb + 5].f32;
            float m20 = vm->locals[mb + 6].f32;
            float m21 = vm->locals[mb + 7].f32;
            float m22 = vm->locals[mb + 8].f32;

            float x = vm->locals[vb + 0].f32;
            float y = vm->locals[vb + 1].f32;
            float z = vm->locals[vb + 2].f32;

            mx_word_t rx, ry, rz;
            rx.f32 = fmaf(m00, x, fmaf(m01, y, m02 * z));
            ry.f32 = fmaf(m10, x, fmaf(m11, y, m12 * z));
            rz.f32 = fmaf(m20, x, fmaf(m21, y, m22 * z));

            if (!mx_push(vm, rx)) return 0;
            if (!mx_push(vm, ry)) return 0;
            if (!mx_push(vm, rz)) return 0;
            break;
        }

        case MX_A2P2L: {
            uint8_t ab = vm->program[vm->pc++];
            uint8_t pb = vm->program[vm->pc++];
            if (!mx_check_local(vm, ab, 6) || !mx_check_local(vm, pb, 2)) return 0;

            float a  = vm->locals[ab + 0].f32;
            float b  = vm->locals[ab + 1].f32;
            float tx = vm->locals[ab + 2].f32;
            float c  = vm->locals[ab + 3].f32;
            float d  = vm->locals[ab + 4].f32;
            float ty = vm->locals[ab + 5].f32;

            float x  = vm->locals[pb + 0].f32;
            float y  = vm->locals[pb + 1].f32;

            mx_word_t ox, oy;
            ox.f32 = fmaf(a, x, fmaf(b, y, tx));
            oy.f32 = fmaf(c, x, fmaf(d, y, ty));

            if (!mx_push(vm, ox)) return 0;
            if (!mx_push(vm, oy)) return 0;
            break;
        }

        case MX_SPR2L: {
            uint8_t ab = vm->program[vm->pc++];
            uint8_t sb = vm->program[vm->pc++];
            uint8_t fl = vm->program[vm->pc++];

            if (!mx_check_local(vm, ab, 6) || !mx_check_local(vm, sb, 4)) return 0;

            float a  = vm->locals[ab + 0].f32;
            float b  = vm->locals[ab + 1].f32;
            float tx = vm->locals[ab + 2].f32;
            float c  = vm->locals[ab + 3].f32;
            float d  = vm->locals[ab + 4].f32;
            float ty = vm->locals[ab + 5].f32;

            float w  = vm->locals[sb + 0].f32;
            float h  = vm->locals[sb + 1].f32;
            float ax = vm->locals[sb + 2].f32;
            float ay = vm->locals[sb + 3].f32;

            float x0 = -ax * w;
            float y0 = -ay * h;
            float x1 = (1.0f - ax) * w;
            float y1 = y0;
            float x2 = x1;
            float y2 = (1.0f - ay) * h;
            float x3 = x0;
            float y3 = y2;

            float p[8];
            p[0] = fmaf(a, x0, fmaf(b, y0, tx));
            p[1] = fmaf(c, x0, fmaf(d, y0, ty));
            p[2] = fmaf(a, x1, fmaf(b, y1, tx));
            p[3] = fmaf(c, x1, fmaf(d, y1, ty));
            p[4] = fmaf(a, x2, fmaf(b, y2, tx));
            p[5] = fmaf(c, x2, fmaf(d, y2, ty));
            p[6] = fmaf(a, x3, fmaf(b, y3, tx));
            p[7] = fmaf(c, x3, fmaf(d, y3, ty));

            if (fl & 0x02) {
                float xmin = p[0], xmax = p[0];
                float ymin = p[1], ymax = p[1];
                for (int i = 2; i < 8; i += 2) {
                    if (p[i] < xmin) xmin = p[i];
                    if (p[i] > xmax) xmax = p[i];
                    if (p[i + 1] < ymin) ymin = p[i + 1];
                    if (p[i + 1] > ymax) ymax = p[i + 1];
                }

                mx_word_t o;
                o.f32 = xmin; if (!mx_push(vm, o)) return 0;
                o.f32 = ymin; if (!mx_push(vm, o)) return 0;
                o.f32 = xmax; if (!mx_push(vm, o)) return 0;
                o.f32 = ymax; if (!mx_push(vm, o)) return 0;
            } else {
                for (int i = 0; i < 8; ++i) {
                    mx_word_t o;
                    o.f32 = p[i];
                    if (!mx_push(vm, o)) return 0;
                }
            }
            break;
        }

        case MX_CMPZ: {
            mx_word_t a, r;
            if (!mx_pop(vm, &a)) return 0;
            r.f32 = (a.f32 == 0.0f) ? 1.0f : 0.0f;
            if (!mx_push(vm, r)) return 0;
            break;
        }

        case MX_FCMPLT:
        case MX_FCMPGT: {
            mx_word_t b, a, r;
            if (!mx_pop(vm, &b) || !mx_pop(vm, &a)) return 0;
            if (op == MX_FCMPLT) r.f32 = (a.f32 < b.f32) ? 1.0f : 0.0f;
            else                 r.f32 = (a.f32 > b.f32) ? 1.0f : 0.0f;
            if (!mx_push(vm, r)) return 0;
            break;
        }

        case MX_JMP:
        case MX_JZ:
        case MX_JNZ: {
            int8_t rel = rd_i8(&vm->program[vm->pc++]);

            if (op == MX_JMP) {
                vm->pc = (uint8_t)(vm->pc + rel);
                break;
            }

            mx_word_t cond;
            if (!mx_pop(vm, &cond)) return 0;

            int take = (op == MX_JZ) ? (cond.f32 == 0.0f)
                                     : (cond.f32 != 0.0f);
            if (take) vm->pc = (uint8_t)(vm->pc + rel);
            break;
        }

        case MX_SELECT: {
            mx_word_t b, a, cnd;
            if (!mx_pop(vm, &b) || !mx_pop(vm, &a) || !mx_pop(vm, &cnd)) return 0;
            if (!mx_push(vm, (cnd.f32 != 0.0f) ? a : b)) return 0;
            break;
        }

        default:
            vm->status = MX_ERR_BAD_OPCODE;
            return 0;
        }
    }

    vm->status = MX_ERR_PROGRAM;
    return 0;
}

12. Walidacja nagłówka i załadowanie ramki

static int mx_load_frame(mx_vm_t *vm,
                         const uint8_t *frame,
                         uint16_t frame_len)
{
    if (frame_len < sizeof(mx_header_t)) {
        vm->status = MX_ERR_HEADER;
        return 0;
    }

    memcpy(&vm->hdr, frame, sizeof(mx_header_t));

    if (vm->hdr.magic != 0x4D) {
        vm->status = MX_ERR_MAGIC;
        return 0;
    }

    if (vm->hdr.version != 1) {
        vm->status = MX_ERR_VERSION;
        return 0;
    }

    if (vm->hdr.hdr_size != sizeof(mx_header_t)) {
        vm->status = MX_ERR_HEADER;
        return 0;
    }

    if (vm->hdr.local_words > MX_MAX_LOCALS ||
        vm->hdr.stack_words > MX_MAX_STACK ||
        vm->hdr.prog_len > MX_MAX_PROG ||
        vm->hdr.out_words > MX_MAX_OUT) {
        vm->status = MX_ERR_HEADER;
        return 0;
    }

    uint16_t need =
        sizeof(mx_header_t) +
        ((uint16_t)vm->hdr.local_words * 4) +
        vm->hdr.prog_len;

    if (frame_len < need) {
        vm->status = MX_ERR_HEADER;
        return 0;
    }

    const uint8_t *p = frame + sizeof(mx_header_t);

    memcpy(vm->locals, p, vm->hdr.local_words * 4);
    p += vm->hdr.local_words * 4;

    memcpy(vm->program, p, vm->hdr.prog_len);

    vm->status = MX_OK;
    return 1;
}

13. Integracja z RIA: handler operacji

Po stronie firmware RIA handler robi logicznie to:

    pobierz całą ramkę z XSTACK,

    zwaliduj,

    wykonaj interpreter,

    wpisz status do A,

    wpisz outc do X,

    wypchnij wynik na XSTACK.

To korzysta bezpośrednio z istniejącego modelu OP + RETURN/BUSY + A/X, który RIA dokumentuje.

Szkic:

void os_op_mathvm(void) {
    mx_vm_t vm;
    uint8_t frame_buf[16 + 48*4 + 160];
    uint16_t frame_len = xstack_pull_frame(frame_buf, sizeof(frame_buf)); // czyta najpierw header, potem resztę

    if (!mx_load_frame(&vm, frame_buf, frame_len)) {
        os_set_ax(vm.status, 0);
        return;
    }

    if (!mx_exec(&vm)) {
        os_set_ax(vm.status, 0);
        return;
    }

    // opcjonalne pakowanie wyniku do int16
    if (vm.hdr.flags & MX_FLAG_RETURN_I16) {
        // v1: każdy word wyniku zamieniany na jeden int16 zapisany w low 16 bitach worda,
        // high 16 bitów = 0; X nadal oznacza liczbę wordów, nie liczbę bajtów
    }

    xstack_push_words((const uint32_t *)vm.out, vm.outc); // Twoja funkcja
    os_set_ax(MX_OK, vm.outc);
}

14. Przykład binarny: mat3 × vec3
14.1. Locals

0..8   = mat3
9..11  = vec3

14.2. Program symboliczny

M3V3L 0,9
RET   3

14.3. Bajty programu

3E 00 09 02 03

14.4. Pełna ramka logiczna

mx_header_t h = {
    .magic       = 0x4D,
    .version     = 1,
    .flags       = 0,
    .hdr_size    = 16,
    .prog_len    = 5,
    .local_words = 12,
    .out_words   = 3,
    .stack_words = 8,
    .xram_in     = 0xFFFF,
    .xram_out    = 0xFFFF,
    .count       = 1,
    .reserved    = 0
};

15. Przykład binarny: transformacja sprite’a
15.1. Locals

0 = a
1 = b
2 = tx
3 = c
4 = d
5 = ty
6 = w
7 = h
8 = ax
9 = ay

15.2. Program: zwróć 4 rogi

SPR2L 0,6,0x01
RET   8

Bajty:

48 00 06 01 02 08

15.3. Program: zwróć bbox

SPR2L 0,6,0x02
RET   4

Bajty:

48 00 06 02 02 04

16. Batch mode przez XRAM

Tu warto wykorzystać to, że RIA ma 64 KB XRAM oraz dwa portale RW0/RW1 z auto-increment, co dobrze nadaje się do sekwencyjnego czytania i pisania buforów.
16.1. Propozycja v2

To jest wyraźnie poza zakresem v1.

Dla count > 1 w v2:

    xram_in wskazuje pierwszy rekord wejściowy,

    xram_out wskazuje pierwszy rekord wyjściowy,

    rekord ma stały stride zależny od programu,

    interpreter uruchamia tę samą sekwencję count razy.

Na początek zrobiłbym dwa osobne tryby batch zamiast pełnej ogólności:

    M3V3L_BATCH

    SPR2L_BATCH

To będzie prostsze i szybsze niż pełny ogólny VM loop.
17. Minimalny pakowacz po stronie 6502 / hosta

Jeśli składasz ramkę po stronie C hosta:

typedef struct {
    uint8_t data[16 + 48*4 + 160];
    uint16_t len;
} mx_frame_builder_t;

static void fb_u8(mx_frame_builder_t *b, uint8_t v) {
    b->data[b->len++] = v;
}

static void fb_u16(mx_frame_builder_t *b, uint16_t v) {
    b->data[b->len++] = (uint8_t)(v & 0xFF);
    b->data[b->len++] = (uint8_t)(v >> 8);
}

static void fb_u32(mx_frame_builder_t *b, uint32_t v) {
    b->data[b->len++] = (uint8_t)(v);
    b->data[b->len++] = (uint8_t)(v >> 8);
    b->data[b->len++] = (uint8_t)(v >> 16);
    b->data[b->len++] = (uint8_t)(v >> 24);
}

static void fb_f32(mx_frame_builder_t *b, float f) {
    union { float f; uint32_t u; } u = { .f = f };
    fb_u32(b, u.u);
}

A potem przy wysyłaniu do XSTACK:

for (int i = b.len - 1; i >= 0; --i) {
    *(volatile uint8_t*)0xFFEC = b.data[i];
}
*(volatile uint8_t*)0xFFEF = RIA_OP_MATHVM;
/* JSR $FFF1 po stronie 6502 asm */

18. Co bym wdrożył najpierw

Najkrótsza ścieżka do działającej wersji:
v1

    NOP

    HALT

    RET

    PUSHF

    LDS, LDV2, LDV3

    DUP, DROP, SWAP

    FADD, FSUB, FMUL, FDIV, FMADD

    FNEG, FABS

    FSIN, FCOS, FSQRT

    FMIN, FMAX

    FROUND, FTRUNC

    M3V3L

    A2P2L

    SPR2L

v1.1

    CMPZ, JZ, JNZ, SELECT

v2

    M3M3L

    V3DOT, CROSS3, NORM3

    batch mode przez XRAM

To da najlepszy stosunek:

    prostota implementacji,

    mały interpreter,

    duży spadek liczby round-tripów przez RIA.

19. Najważniejsza decyzja projektowa

Nie robiłbym w pierwszej wersji „pełnego języka matematycznego”.
Zrobiłbym hybrydę:

    mały stack VM,

    plus kilka grubych instrukcji domenowych:

        M3V3L

        A2P2L

        SPR2L

To jest dokładnie ten kompromis, który ma sens przy kosztownym mechanizmie wywołań przez XSTACK i OP. Sam mechanizm wywołania i pamięci już daje Ci dobry fundament do takiej warstwy.

Mogę w następnym kroku rozpisać gotowy zestaw plików:

    src/mathvm/mathvm.h

    src/mathvm/mathvm_host.c

    src/mathvm/mathvm_ria.c

    oraz przykładowy test mat3xvec3 i sprite_bbox.
