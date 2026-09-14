//==============================================================================
/*
    Aplicacion haptica para tarea de predominancia cognitiva (Test de Stroop).
    Desarrollado con CHAI3D para el dispositivo Novint Falcon.

    Objetivo: Evaluacion del control inhibitorio y tiempos de respuesta bajo
    condiciones de fatiga. Registro de metricas espaciotemporales (fases de
    reaccion y movimiento) y desviaciones de trayectoria a 1000 Hz.
*/
//==============================================================================

#include "chai3d.h"
#include <GLFW/glfw3.h>
#include <fstream> 
#include <iostream>
#include <iomanip> 
#include <filesystem>
#include <vector>
#include <string>
#include <locale>
#include <cstdlib>
#include <ctime>  
#include <cmath> 
#include <sstream> 

using namespace chai3d;
using namespace std; 
namespace fs = std::filesystem;

// Permite usar la coma como separador decimal en la exportacion a CSV
struct CommaFacet : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
};

//------------------------------------------------------------------------------
// ESTADOS DEL JUEGO Y DIFICULTAD
//------------------------------------------------------------------------------
enum GameState {
    STATE_MENU_DIFF,
    STATE_MENU_TRIALS,
    STATE_MENU_FATIGUE,
    STATE_WAITING_FOR_START,
    STATE_PLAYING,
    STATE_FINISHED
};

enum Difficulty {
    DIFF_NORMAL,
    DIFF_HARD
};

GameState currentState = STATE_MENU_DIFF;
Difficulty selectedDifficulty = DIFF_NORMAL;
int maxTrials = 10;
string fatigueType = "";
string subjectID = "";
bool isCruzada = false;

// Friccion nula para minimizar la carga fisica y enfatizar el proceso de decision
const double ICE_FRICTION = 0.0;

//------------------------------------------------------------------------------
// GENERAL SETTINGS
//------------------------------------------------------------------------------
cStereoMode stereoMode = C_STEREO_DISABLED;
bool fullscreen = false;
bool mirroredDisplay = false;

//------------------------------------------------------------------------------
// VARIABLES DE GRABACION CSV Y BOTON
//------------------------------------------------------------------------------
struct RecordData {
    double time;
    double deviceX, deviceY, deviceZ;
    double forceX, forceY, forceZ;
    int trialID;            // Identificador de la palabra actual
    int fase;               // 0=Vuelta, 1=Pensando(Centro), 2=Moviendo
    bool inAir;
    int currentFailCount;
    double currentAirTime;

    // NOTA: Se mantiene la nomenclatura "Wall" del codigo base fisico, 
    // pero en esta tarea evalua la perdida del control inhibitorio (intrusion en cuadrante erroneo)
    bool isFailingWall;     // 1 si esta en cuadrante erroneo
    int currentWallFails;   // Contador de desvios cognitivos
    double currentWallTime; // Tiempo perdido en cuadrantes erroneos
};

std::vector<RecordData> recordedData;
volatile bool isRecording = false;
cPrecisionClock recordClock;
bool lastButtonState = false;

struct ErrorEvent {
    double startTime;
    double endTime;
};
std::vector<ErrorEvent> airErrorEvents;
std::vector<ErrorEvent> wallErrorEvents;

//------------------------------------------------------------------------------
// VARIABLES DE LOGICA DE SEGUIMIENTO Y STROOP
//------------------------------------------------------------------------------
volatile bool isInAir = false;
volatile int failCount = 0;
volatile double totalAirTime = 0.0;
bool wasTouching = true;

volatile bool isFailingWall = false;
volatile int wallFailCount = 0;
volatile double totalWallTime = 0.0;
bool wasFailingWall = false;

volatile bool resetTracking = false;
double lastRecordTime = 0.0;

// Radios de las zonas de deteccion
const double START_ZONE_RADIUS = 0.05;
const double TARGET_ZONE_RADIUS = 0.07;

volatile int currentTrialID = 0;
volatile int currentFase = 0;

// Variables de Stroop
// 0 = Rojo, 1 = Amarillo, 2 = Azul, 3 = Verde
int currentFontColor = -1;
int currentWordColor = -1;
int lastFontColor = -1;
bool waitingForCenter = false;

//------------------------------------------------------------------------------
// DECLARED VARIABLES (CHAI3D)
//------------------------------------------------------------------------------
cWorld* world;
cCamera* camera;
cViewport* viewport = nullptr;
cSpotLight* light;
cHapticDeviceHandler* handler;
cGenericHapticDevicePtr hapticDevice;
cToolCursor* tool;

cMesh* workspacePlane;
cMesh* startCircle;
cMesh* targetCircleRed;
cMesh* targetCircleYellow;
cMesh* targetCircleBlue;
cMesh* targetCircleGreen;

cFontPtr font;
cFontPtr fontLarge;
cLabel* labelRates;

// UI Menu Dificultad
cLabel* labelMenuTitle;
cLabel* labelBtnNormal;
cLabel* labelBtnHard;
cLabel* labelInstructions;

// UI Menu Cantidad de Ensayos
cLabel* labelTrialsTitle;
cLabel* labelBtn10;
cLabel* labelBtn20;
cLabel* labelBtn30;

// UI Menu Fatiga
cLabel* labelFatigueTitle;
cLabel* labelBtnPre;
cLabel* labelBtnPost;
cLabel* labelBtnPractica;

// UI General
cLabel* labelMessage;
cLabel* labelBtnBack;
cLabel* labelStroop;
cLabel* labelErrorCross;
cBackground* background;

bool simulationRunning = false;
bool simulationFinished = true;
cFrequencyCounter freqCounterGraphics;
cFrequencyCounter freqCounterHaptics;
cThread* hapticsThread;

GLFWwindow* window = nullptr;
int windowW = 0;
int windowH = 0;
int framebufferW = 0;
int framebufferH = 0;
int swapInterval = 1;

double maxStiffness = 0.0;
double toolRadius = 0.01;

//------------------------------------------------------------------------------
// DECLARED FUNCTIONS
//------------------------------------------------------------------------------
void onWindowSizeCallback(GLFWwindow* a_window, int a_width, int a_height);
void onFrameBufferSizeCallback(GLFWwindow* a_window, int a_width, int a_height);
void onErrorCallback(int a_error, const char* a_description);
void onKeyCallback(GLFWwindow* a_window, int a_key, int a_scancode, int a_action, int a_mods);
void onMouseButtonCallback(GLFWwindow* a_window, int a_button, int a_action, int a_mods);
void onWindowContentScaleCallback(GLFWwindow* a_window, float a_xscale, float a_yscale);
void renderGraphics(void);
void renderHaptics(void);
void close(void);
void startGame();
void resetToMenu();
void generateNextStroop();

// Funciones para manejar visibilidad de los menus
void showDifficultyMenu();
void showTrialsMenu();
void showFatigueMenu();

// Funcion auxiliar para detectar clics en los botones de texto
bool hitButton(cLabel* btn, double x, double y) {
    if (!btn->getShowEnabled()) return false;
    double bx = btn->getLocalPos().x();
    double by = btn->getLocalPos().y();
    double bw = btn->getWidth();
    double bh = btn->getHeight();
    return (x >= bx && x <= bx + bw && y >= by && y <= by + bh);
}


//------------------------------------------------------------------------------
// LOGICA TEST DE STROOP
// Genera ensayos asegurando una carga cognitiva constante mediante dos reglas:
// 1. Incongruencia obligatoria: El color de la fuente y el texto nunca coinciden.
// 2. Sin repeticion: El color objetivo no se repite en ensayos consecutivos 
//    para evitar que el usuario se anticipe o se habitue al movimiento.
//------------------------------------------------------------------------------
void generateNextStroop() {
    int numColors = (selectedDifficulty == DIFF_HARD) ? 4 : 3;

    // 1. Elegir el color de la FUENTE (Objetivo haptico real)
    int newFontColorInt;
    do {
        newFontColorInt = rand() % numColors;
    } while (newFontColorInt == lastFontColor);

    currentFontColor = newFontColorInt;
    lastFontColor = currentFontColor;

    // 2. Elegir la PALABRA
    int newWordColorInt;
    do {
        newWordColorInt = rand() % numColors;
    } while (newWordColorInt == currentFontColor);

    currentWordColor = newWordColorInt;

    // 3. Asignar el texto
    switch (currentWordColor) {
    case 0: labelStroop->setText("  ROJO  "); break;
    case 1: labelStroop->setText("AMARILLO"); break;
    case 2: labelStroop->setText("  AZUL  "); break;
    case 3: labelStroop->setText("  VERDE "); break;
    }

    // 4. Asignar el color visual
    switch (currentFontColor) {
    case 0: labelStroop->m_fontColor.setRed(); break;
    case 1: labelStroop->m_fontColor.setYellow(); break;
    case 2: labelStroop->m_fontColor.setBlue(); break;
    case 3: labelStroop->m_fontColor.setGreen(); break;
    }
}

//==============================================================================
// MAIN
//==============================================================================
int main(int argc, char* argv[])
{
    cout << "Iniciando simulacion con Novint Falcon..." << endl;

    cout << "--------------------------------------------------" << endl;
    cout << "Seleccione el Tipo de Experimento:" << endl;
    cout << "  1 = Normal" << endl;
    cout << "  2 = Cruzada" << endl;
    cout << "Opcion: ";
    int opcionExp;
    cin >> opcionExp;

    if (opcionExp == 2) {
        isCruzada = true;
        cout << "-> Modo CRUZADA seleccionado." << endl;
    }
    else {
        isCruzada = false;
        cout << "-> Modo NORMAL seleccionado." << endl;
    }

    cout << "--------------------------------------------------" << endl;
    cout << "Introduce el ID del sujeto (ej. S01) y pulsa Enter: ";
    cin >> subjectID;
    cout << "Registrado sujeto: " << subjectID << endl;
    cout << "Abriendo ventana grafica..." << endl;
    cout << "--------------------------------------------------" << endl;

    srand((unsigned)time(NULL));

    if (!glfwInit()) return 1;
    glfwSetErrorCallback(onErrorCallback);

    const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    windowW = 0.8 * mode->height;
    windowH = 0.6 * mode->height;
    int x = 0.5 * (mode->width - windowW);
    int y = 0.5 * (mode->height - windowH);

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    window = glfwCreateWindow(windowW, windowH, "Chai3D - Stroop Test (Multi-Fase + Datos)", NULL, NULL);
    if (!window) { glfwTerminate(); return 1; }

    glfwSetKeyCallback(window, onKeyCallback);
    glfwSetMouseButtonCallback(window, onMouseButtonCallback);
    glfwSetWindowSizeCallback(window, onWindowSizeCallback);
    glfwSetFramebufferSizeCallback(window, onFrameBufferSizeCallback);
    glfwSetWindowContentScaleCallback(window, onWindowContentScaleCallback);

    glfwGetFramebufferSize(window, &framebufferW, &framebufferH);
    glfwSetWindowPos(window, x, y);
    glfwSetWindowSize(window, windowW, windowH);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(swapInterval);

#ifdef GLEW_VERSION
    if (glewInit() != GLEW_OK) { glfwTerminate(); return 1; }
#endif

    world = new cWorld();
    world->m_backgroundColor.setBlack();

    camera = new cCamera(world);
    world->addChild(camera);
    camera->set(cVector3d(0.0, 0.0, 1.0), cVector3d(0.0, 0.0, 0.0), cVector3d(0.0, 1.0, 0.0));
    camera->setClippingPlanes(0.01, 10.0);

    light = new cSpotLight(world);
    world->addChild(light);
    light->setEnabled(true);
    light->setLocalPos(0.0, 0.0, 0.7);
    light->setDir(0.0, 0.0, -1.0);

    handler = new cHapticDeviceHandler();
    handler->getDevice(hapticDevice, 0);
    cHapticDeviceInfo hapticDeviceInfo = hapticDevice->getSpecifications();
    hapticDevice->setEnableGripperUserSwitch(true);

    tool = new cToolCursor(world);
    camera->addChild(tool);
    tool->setLocalPos(-1.0, 0.0, 0.0);
    tool->setHapticDevice(hapticDevice);
    tool->setRadius(toolRadius);
    tool->setWorkspaceRadius(1.0);
    tool->setWaitForSmallForce(true);
    tool->start();

    // Ajuste de la rigidez maxima para mantener la estabilidad mecanica del dispositivo
    maxStiffness = hapticDeviceInfo.m_maxLinearStiffness / tool->getWorkspaceScaleFactor();

    // Creacion del Plano Liso
    workspacePlane = new cMesh();
    cCreatePlane(workspacePlane, 1.4, 1.4);
    workspacePlane->createAABBCollisionDetector(toolRadius);
    world->addChild(workspacePlane);
    workspacePlane->setLocalPos(0.0, 0.0, 0.0);
    workspacePlane->setUseTexture(false);
    workspacePlane->m_material->setGrayLight();
    workspacePlane->m_material->setHapticTriangleSides(true, false);
    workspacePlane->setShowEnabled(false);

    // Creacion de objetivos espaciales (Circulos de colores).
    // Situados a distancias radiales identicas para garantizar tiempos de trayectoria equivalentes. 
    // Los pares complementarios se oponen en diagonal para maximizar el contraste visual 
    // y asegurar que los desvios sean fallos inhibitorios y no de percepcion.
    
    // Circulo Central Blanco
    startCircle = new cMesh();
    cCreateCylinder(startCircle, 0.0005, START_ZONE_RADIUS);
    world->addChild(startCircle);
    startCircle->setLocalPos(0.0, 0.0, 0.0005);
    startCircle->m_material->setWhite();
    startCircle->setTransparencyLevel(0.6f);
    startCircle->setUseTransparency(true);
    startCircle->setShowEnabled(false);

    // Circulo Rojo
    targetCircleRed = new cMesh();
    cCreateCylinder(targetCircleRed, 0.0005, TARGET_ZONE_RADIUS);
    world->addChild(targetCircleRed);
    targetCircleRed->setLocalPos(-0.50, 0.30, 0.0005);
    targetCircleRed->m_material->setRed();
    targetCircleRed->setTransparencyLevel(0.6f);
    targetCircleRed->m_material->m_specular.setBlack();
    targetCircleRed->setShowEnabled(false);

    // Circulo Amarillo
    targetCircleYellow = new cMesh();
    cCreateCylinder(targetCircleYellow, 0.0005, TARGET_ZONE_RADIUS);
    world->addChild(targetCircleYellow);
    targetCircleYellow->setLocalPos(0.50, 0.30, 0.0005);
    targetCircleYellow->m_material->setYellow();
    targetCircleYellow->setTransparencyLevel(0.6f);
    targetCircleYellow->m_material->m_specular.setBlack();
    targetCircleYellow->setShowEnabled(false);

    // Circulo Azul
    targetCircleBlue = new cMesh();
    cCreateCylinder(targetCircleBlue, 0.0005, TARGET_ZONE_RADIUS);
    world->addChild(targetCircleBlue);
    targetCircleBlue->setLocalPos(-0.50, -0.30, 0.0005);
    targetCircleBlue->m_material->setBlue();
    targetCircleBlue->setTransparencyLevel(0.6f);
    targetCircleBlue->m_material->m_specular.setBlack();
    targetCircleBlue->setShowEnabled(false);

    // Circulo Verde
    targetCircleGreen = new cMesh();
    cCreateCylinder(targetCircleGreen, 0.0005, TARGET_ZONE_RADIUS);
    world->addChild(targetCircleGreen);
    targetCircleGreen->setLocalPos(0.50, -0.30, 0.0005);
    targetCircleGreen->m_material->setGreen();
    targetCircleGreen->setTransparencyLevel(0.6f);
    targetCircleGreen->m_material->m_specular.setBlack();
    targetCircleGreen->setShowEnabled(false);

    // Interfaz de Texto
    font = NEW_CFONT_CALIBRI_20();
    fontLarge = NEW_CFONT_CALIBRI_32();
    cFontPtr fontInstrucciones = NEW_CFONT_CALIBRI_24();

    labelRates = new cLabel(font);
    camera->m_frontLayer->addChild(labelRates);

    // Etiquetas Menu 1: Dificultad
    labelMenuTitle = new cLabel(fontLarge);
    labelMenuTitle->setText("SELECCIONA LA DIFICULTAD DE LA TAREA:");
    labelMenuTitle->m_fontColor.setWhite();
    camera->m_frontLayer->addChild(labelMenuTitle);

    labelBtnNormal = new cLabel(fontLarge);
    labelBtnNormal->setText("[ NORMAL (3 Circulos) ]");
    labelBtnNormal->m_fontColor.setGreen();
    camera->m_frontLayer->addChild(labelBtnNormal);

    labelBtnHard = new cLabel(fontLarge);
    labelBtnHard->setText("[ DIFICIL (4 Circulos) ]");
    labelBtnHard->m_fontColor.setRed();
    camera->m_frontLayer->addChild(labelBtnHard);

    labelInstructions = new cLabel(fontInstrucciones);
    labelInstructions->setText("Muevase al color de la TINTA, no a la palabra. Vuelva siempre al centro.");
    labelInstructions->m_fontColor.setWhite();
    camera->m_frontLayer->addChild(labelInstructions);

    // Etiquetas Menu 2: Ensayos
    labelTrialsTitle = new cLabel(fontLarge);
    labelTrialsTitle->setText("SELECCIONA EL NUMERO DE PALABRAS A PROBAR:");
    labelTrialsTitle->m_fontColor.setWhite();
    labelTrialsTitle->setShowEnabled(false);
    camera->m_frontLayer->addChild(labelTrialsTitle);

    labelBtn10 = new cLabel(fontLarge);
    labelBtn10->setText("[ 10 PALABRAS ]");
    labelBtn10->m_fontColor.setGreen();
    labelBtn10->setShowEnabled(false);
    camera->m_frontLayer->addChild(labelBtn10);

    labelBtn20 = new cLabel(fontLarge);
    labelBtn20->setText("[ 20 PALABRAS ]");
    labelBtn20->m_fontColor.setBlue();
    labelBtn20->setShowEnabled(false);
    camera->m_frontLayer->addChild(labelBtn20);

    labelBtn30 = new cLabel(fontLarge);
    labelBtn30->setText("[ 30 PALABRAS ]");
    labelBtn30->m_fontColor.setRed();
    labelBtn30->setShowEnabled(false);
    camera->m_frontLayer->addChild(labelBtn30);

    // Etiquetas Menu 3: Fatiga
    labelFatigueTitle = new cLabel(fontLarge);
    labelFatigueTitle->setText("SELECCIONA EL TIPO DE GRABACION:");
    labelFatigueTitle->m_fontColor.setWhite();
    labelFatigueTitle->setShowEnabled(false);
    camera->m_frontLayer->addChild(labelFatigueTitle);

    labelBtnPre = new cLabel(fontLarge);
    labelBtnPre->setText("[ PRE-FATIGA ]");
    labelBtnPre->m_fontColor.setGreen();
    labelBtnPre->setShowEnabled(false);
    camera->m_frontLayer->addChild(labelBtnPre);

    labelBtnPost = new cLabel(fontLarge);
    labelBtnPost->setText("[ POST-FATIGA ]");
    labelBtnPost->m_fontColor.setRed();
    labelBtnPost->setShowEnabled(false);
    camera->m_frontLayer->addChild(labelBtnPost);

    labelBtnPractica = new cLabel(fontLarge);
    labelBtnPractica->setText("[ PRACTICA ]");
    labelBtnPractica->m_fontColor.setYellow();
    labelBtnPractica->setShowEnabled(false);
    camera->m_frontLayer->addChild(labelBtnPractica);

    // Etiquetas Comunes/Juego
    labelMessage = new cLabel(fontLarge);
    labelMessage->setText("");
    labelMessage->m_fontColor.setWhite();
    labelMessage->setShowEnabled(false);
    camera->m_frontLayer->addChild(labelMessage);

    labelBtnBack = new cLabel(fontLarge);
    labelBtnBack->setText("[ <-- VOLVER AL MENU ]");
    labelBtnBack->m_fontColor.setWhite();
    labelBtnBack->setShowEnabled(false);
    camera->m_frontLayer->addChild(labelBtnBack);

    labelStroop = new cLabel(fontLarge);
    labelStroop->setText("");
    labelStroop->setFontScale(1.5);
    labelStroop->setShowEnabled(false);
    camera->m_frontLayer->addChild(labelStroop);

    // Etiqueta para la Cruz de Error
    labelErrorCross = new cLabel(fontLarge);
    labelErrorCross->setText("X");
    labelErrorCross->setFontScale(2.5);
    labelErrorCross->m_fontColor.setWhite();
    labelErrorCross->setShowEnabled(false);
    camera->m_frontLayer->addChild(labelErrorCross);

    background = new cBackground();
    camera->m_backLayer->addChild(background);
    background->setCornerColors(cColorf(0.1, 0.1, 0.1), cColorf(0.1, 0.1, 0.1), cColorf(0.0, 0.0, 0.0), cColorf(0.0, 0.0, 0.0));

    viewport = new cViewport(camera);

    // Separacion del bucle haptico (1000 Hz) para garantizar captura de biomarcadores sin latencia
    hapticsThread = new cThread();
    hapticsThread->start(renderHaptics, CTHREAD_PRIORITY_HAPTICS);
    atexit(close);

    showDifficultyMenu(); // Inicializar estado visual

    while (!glfwWindowShouldClose(window))
    {
        renderGraphics();
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

//------------------------------------------------------------------------------
// VISIBILIDAD DE MENUS
//------------------------------------------------------------------------------
void showDifficultyMenu() {
    currentState = STATE_MENU_DIFF;

    labelTrialsTitle->setShowEnabled(false);
    labelBtn10->setShowEnabled(false); labelBtn20->setShowEnabled(false); labelBtn30->setShowEnabled(false);

    labelFatigueTitle->setShowEnabled(false);
    labelBtnPre->setShowEnabled(false); labelBtnPost->setShowEnabled(false);

    labelBtnBack->setShowEnabled(false);

    labelMenuTitle->setShowEnabled(true);
    labelInstructions->setShowEnabled(true);
    labelBtnNormal->setShowEnabled(true);
    labelBtnHard->setShowEnabled(true);
    labelBtnPractica->setShowEnabled(false);
}

void showTrialsMenu() {
    currentState = STATE_MENU_TRIALS;

    labelMenuTitle->setShowEnabled(false);
    labelInstructions->setShowEnabled(false);
    labelBtnNormal->setShowEnabled(false);
    labelBtnHard->setShowEnabled(false);

    labelFatigueTitle->setShowEnabled(false);
    labelBtnPre->setShowEnabled(false); 
    labelBtnPost->setShowEnabled(false);
    labelBtnPractica->setShowEnabled(false);

    labelTrialsTitle->setShowEnabled(true);
    labelBtn10->setShowEnabled(true);
    labelBtn20->setShowEnabled(true);
    labelBtn30->setShowEnabled(true);

    labelBtnBack->setShowEnabled(true);
}

void showFatigueMenu() {
    currentState = STATE_MENU_FATIGUE;

    labelTrialsTitle->setShowEnabled(false);
    labelBtn10->setShowEnabled(false); labelBtn20->setShowEnabled(false); labelBtn30->setShowEnabled(false);

    labelFatigueTitle->setShowEnabled(true);
    labelBtnPre->setShowEnabled(true);
    labelBtnPost->setShowEnabled(true);
    labelBtnPractica->setShowEnabled(true);

    labelBtnBack->setShowEnabled(true);
}

//------------------------------------------------------------------------------
void resetToMenu()
{
    isRecording = false;
    recordClock.stop();

    resetTracking = true;
    isInAir = false;
    isFailingWall = false;
    waitingForCenter = false;

    airErrorEvents.clear();
    wallErrorEvents.clear();

    workspacePlane->setShowEnabled(false);
    startCircle->setShowEnabled(false);
    targetCircleRed->setShowEnabled(false);
    targetCircleYellow->setShowEnabled(false);
    targetCircleBlue->setShowEnabled(false);
    targetCircleGreen->setShowEnabled(false);
    labelBtnPractica->setShowEnabled(false);

    labelMessage->setShowEnabled(false);
    labelStroop->setShowEnabled(false);
    labelErrorCross->setShowEnabled(false);
    lastFontColor = -1;

    tool->getHapticPoint(0)->m_sphereProxy->m_material->setWhite();
    background->setCornerColors(cColorf(0.1, 0.1, 0.1), cColorf(0.1, 0.1, 0.1), cColorf(0.0, 0.0, 0.0), cColorf(0.0, 0.0, 0.0));

    fatigueType = "";
    showDifficultyMenu();
}

//------------------------------------------------------------------------------
void startGame()
{
    currentState = STATE_WAITING_FOR_START;

    labelFatigueTitle->setShowEnabled(false);
    labelBtnPre->setShowEnabled(false);
    labelBtnPost->setShowEnabled(false);
    labelBtnPractica->setShowEnabled(false);

    workspacePlane->setShowEnabled(true);
    startCircle->setShowEnabled(true);

    targetCircleRed->setShowEnabled(true);
    targetCircleYellow->setShowEnabled(true);
    targetCircleBlue->setShowEnabled(true);

    if (selectedDifficulty == DIFF_HARD) {
        targetCircleGreen->setShowEnabled(true);
    }

    labelBtnBack->setShowEnabled(true);
    labelMessage->setText("Toca el circulo central y presiona el boton central para empezar.");
    labelMessage->setShowEnabled(true);

    // Aplicacion de friccion nula (Efecto Hielo)
    // Se elimina la resistencia fisica para minimizar la carga motora 
    // y aislar el esfuerzo en los procesos cognitivos y de inhibicion
    double stiffness = 0.5 * maxStiffness;
    workspacePlane->m_material->setStiffness(stiffness);
    workspacePlane->m_material->setStaticFriction(ICE_FRICTION);
    workspacePlane->m_material->setDynamicFriction(ICE_FRICTION);
}

//------------------------------------------------------------------------------
void onMouseButtonCallback(GLFWwindow* a_window, int a_button, int a_action, int a_mods)
{
    if (a_button == GLFW_MOUSE_BUTTON_LEFT && a_action == GLFW_PRESS)
    {
        double xpos, ypos;
        glfwGetCursorPos(a_window, &xpos, &ypos);
        double chaiY = windowH - ypos;

        if (currentState == STATE_MENU_DIFF) {
            if (hitButton(labelBtnNormal, xpos, chaiY)) { selectedDifficulty = DIFF_NORMAL; showTrialsMenu(); }
            else if (hitButton(labelBtnHard, xpos, chaiY)) { selectedDifficulty = DIFF_HARD; showTrialsMenu(); }
        }
        else if (currentState == STATE_MENU_TRIALS) {
            if (hitButton(labelBtn10, xpos, chaiY)) { maxTrials = 10; showFatigueMenu(); }
            else if (hitButton(labelBtn20, xpos, chaiY)) { maxTrials = 20; showFatigueMenu(); }
            else if (hitButton(labelBtn30, xpos, chaiY)) { maxTrials = 30; showFatigueMenu(); }
            else if (hitButton(labelBtnBack, xpos, chaiY)) { showDifficultyMenu(); }
        }
        else if (currentState == STATE_MENU_FATIGUE) {
            if (hitButton(labelBtnPractica, xpos, chaiY)) { fatigueType = "practica"; startGame(); } 
            else if (hitButton(labelBtnPre, xpos, chaiY)) { fatigueType = "pre"; startGame(); }
            else if (hitButton(labelBtnPost, xpos, chaiY)) { fatigueType = "post"; startGame(); }
            else if (hitButton(labelBtnBack, xpos, chaiY)) { showTrialsMenu(); }
        }
        else if (currentState == STATE_WAITING_FOR_START || currentState == STATE_PLAYING || currentState == STATE_FINISHED) {
            if (hitButton(labelBtnBack, xpos, chaiY)) resetToMenu();
        }
    }
}

void onWindowSizeCallback(GLFWwindow* a_window, int a_width, int a_height) { windowW = a_width; windowH = a_height; }
void onFrameBufferSizeCallback(GLFWwindow* a_window, int a_width, int a_height) { framebufferW = a_width; framebufferH = a_height; }
void onWindowContentScaleCallback(GLFWwindow* a_window, float a_xscale, float a_yscale) { if (viewport) viewport->setContentScale(a_xscale, a_yscale); }
void onErrorCallback(int a_error, const char* a_description) { cout << "Error: " << a_description << endl; }
void onKeyCallback(GLFWwindow* a_window, int a_key, int a_scancode, int a_action, int a_mods) {
    if ((a_action != GLFW_PRESS) && (a_action != GLFW_REPEAT)) return;
    if ((a_key == GLFW_KEY_ESCAPE) || (a_key == GLFW_KEY_Q)) glfwSetWindowShouldClose(a_window, GLFW_TRUE);
}

void close(void) {
    simulationRunning = false;
    while (!simulationFinished) { cSleepMs(100); }
    tool->stop();
    delete hapticsThread;
    delete world;
    delete handler;
}

//------------------------------------------------------------------------------
void renderGraphics(void)
{
    if (viewport == nullptr) { return; }

    int displayW = viewport->getDisplayWidth();
    int displayH = viewport->getDisplayHeight();

    labelRates->setText(cStr(freqCounterGraphics.getFrequency(), 0) + " Hz / " + cStr(freqCounterHaptics.getFrequency(), 0) + " Hz");
    labelRates->setLocalPos((int)(0.5 * (displayW - labelRates->getWidth())), 15);

    bool b0 = false, b1 = false, b2 = false, b3 = false;
    hapticDevice->getUserSwitch(0, b0);
    hapticDevice->getUserSwitch(1, b1);
    hapticDevice->getUserSwitch(2, b2);
    hapticDevice->getUserSwitch(3, b3);

    bool spacePressed = (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);
    bool buttonPressed = (b0 || b1 || b2 || b3 || spacePressed);
    bool buttonClicked = (buttonPressed && !lastButtonState);
    lastButtonState = buttonPressed;

    if (currentState == STATE_MENU_DIFF) {
        labelMenuTitle->setLocalPos((displayW - labelMenuTitle->getWidth()) / 2, displayH * 0.85);
        labelInstructions->setLocalPos((displayW - labelInstructions->getWidth()) / 2, displayH * 0.60);
        labelBtnNormal->setLocalPos((displayW - labelBtnNormal->getWidth()) / 2, displayH * 0.35);
        labelBtnHard->setLocalPos((displayW - labelBtnHard->getWidth()) / 2, displayH * 0.25);
    }
    else if (currentState == STATE_MENU_TRIALS) {
        labelTrialsTitle->setLocalPos((displayW - labelTrialsTitle->getWidth()) / 2, displayH * 0.70);
        labelBtn10->setLocalPos((displayW - labelBtn10->getWidth()) / 2, displayH * 0.50);
        labelBtn20->setLocalPos((displayW - labelBtn20->getWidth()) / 2, displayH * 0.40);
        labelBtn30->setLocalPos((displayW - labelBtn30->getWidth()) / 2, displayH * 0.30);
        if (labelBtnBack->getShowEnabled()) labelBtnBack->setLocalPos(20, displayH - 50);
    }
    else if (currentState == STATE_MENU_FATIGUE) {
        labelFatigueTitle->setLocalPos((displayW - labelFatigueTitle->getWidth()) / 2, displayH * 0.70);
        labelBtnPractica->setLocalPos((displayW - labelBtnPractica->getWidth()) / 2, displayH * 0.50); 
        labelBtnPre->setLocalPos((displayW - labelBtnPre->getWidth()) / 2, displayH * 0.40);
        labelBtnPost->setLocalPos((displayW - labelBtnPost->getWidth()) / 2, displayH * 0.30);
        if (labelBtnBack->getShowEnabled()) labelBtnBack->setLocalPos(20, displayH - 50);
    }
    else {
        if (labelBtnBack->getShowEnabled()) labelBtnBack->setLocalPos(20, displayH - 50);
        if (labelMessage->getShowEnabled()) labelMessage->setLocalPos((displayW - labelMessage->getWidth()) / 2, displayH * 0.85);

        if (labelStroop->getShowEnabled()) {
            //labelStroop->setLocalPos((displayW - labelStroop->getWidth()) / 2, (displayH / 2) + 100);
        }
        if (labelErrorCross->getShowEnabled()) {
            labelErrorCross->setLocalPos((displayW - labelErrorCross->getWidth()) / 2, (displayH / 2) + 30);
        }

        if (currentState == STATE_WAITING_FOR_START) {
            cVector3d pos = tool->getDeviceGlobalPos();
            bool isTouchingPreStart = (tool->getDeviceGlobalForce().length() > 0.05);
            double distToCenter = sqrt(pos.x() * pos.x() + pos.y() * pos.y());
            bool isInReferenceCircle = (distToCenter <= START_ZONE_RADIUS);

            if (isTouchingPreStart && isInReferenceCircle) {
                background->setCornerColors(cColorf(0.0, 0.4, 0.0), cColorf(0.0, 0.4, 0.0), cColorf(0.0, 0.1, 0.0), cColorf(0.0, 0.1, 0.0));
            }
            else {
                background->setCornerColors(cColorf(0.4, 0.0, 0.0), cColorf(0.4, 0.0, 0.0), cColorf(0.1, 0.0, 0.0), cColorf(0.1, 0.0, 0.0));
            }

            if (buttonClicked && isInReferenceCircle) {
                currentState = STATE_PLAYING;
                startCircle->setShowEnabled(false);

                labelMessage->setShowEnabled(false);

                cout << "\n--------------------------------------------------" << endl;
                cout << ">>> GRABANDO. La tarea terminara automaticamente..." << endl;
                cout << "--------------------------------------------------\n" << endl;

                background->setCornerColors(cColorf(0.1, 0.1, 0.1), cColorf(0.1, 0.1, 0.1), cColorf(0.0, 0.0, 0.0), cColorf(0.0, 0.0, 0.0));

                recordedData.clear();
                recordedData.reserve(180000);
                airErrorEvents.clear();
                wallErrorEvents.clear();

                recordClock.reset();
                recordClock.start();
                resetTracking = true;
                isRecording = true;

                lastFontColor = -1;
                waitingForCenter = false;
                currentTrialID = 1;
                generateNextStroop();
                labelStroop->setLocalPos((displayW - labelStroop->getWidth()) / 2, (displayH / 2) + 100);
                labelStroop->setShowEnabled(true);
            }
        }
        else if (currentState == STATE_PLAYING) {

            // Feedback visual del error de cuadrante
            labelErrorCross->setShowEnabled(isFailingWall);

            if (isInAir) {
                tool->getHapticPoint(0)->m_sphereProxy->m_material->setRed();
                background->setCornerColors(cColorf(0.4, 0.0, 0.0), cColorf(0.4, 0.0, 0.0), cColorf(0.1, 0.0, 0.0), cColorf(0.1, 0.0, 0.0));
            }
            else {
                tool->getHapticPoint(0)->m_sphereProxy->m_material->setWhite();
                background->setCornerColors(cColorf(0.1, 0.1, 0.1), cColorf(0.1, 0.1, 0.1), cColorf(0.0, 0.0, 0.0), cColorf(0.0, 0.0, 0.0));
            }

            cVector3d currentPos = tool->getDeviceGlobalPos();
            bool isTouchingSurface = (tool->getDeviceGlobalForce().length() > 0.05);
            bool finishedTrials = false;

            if (!waitingForCenter) {
                cVector3d targetPos;
                if (currentFontColor == 0) targetPos = targetCircleRed->getLocalPos();
                else if (currentFontColor == 1) targetPos = targetCircleYellow->getLocalPos();
                else if (currentFontColor == 2) targetPos = targetCircleBlue->getLocalPos();
                else if (currentFontColor == 3) targetPos = targetCircleGreen->getLocalPos();

                double distToTarget = sqrt(pow(currentPos.x() - targetPos.x(), 2) + pow(currentPos.y() - targetPos.y(), 2));

                if (distToTarget <= TARGET_ZONE_RADIUS && isTouchingSurface) {
                    waitingForCenter = true;
                    labelStroop->setShowEnabled(false);
                    startCircle->setShowEnabled(true);
                }
            }
            else {
                double distToCenter = sqrt(pow(currentPos.x(), 2) + pow(currentPos.y(), 2));
                if (distToCenter <= START_ZONE_RADIUS && isTouchingSurface) {
                    waitingForCenter = false;
                    startCircle->setShowEnabled(false);

                    if (currentTrialID >= maxTrials) {
                        finishedTrials = true; // Auto-terminar el juego
                    }
                    else {
                        generateNextStroop();
                        currentTrialID++;
                        labelStroop->setShowEnabled(true);
                    }
                }
            }

            if (buttonClicked || finishedTrials) {
                currentState = STATE_FINISHED;
                isRecording = false;
                recordClock.stop();

                labelStroop->setShowEnabled(false);
                labelErrorCross->setShowEnabled(false);
                startCircle->setShowEnabled(false);
                tool->getHapticPoint(0)->m_sphereProxy->m_material->setWhite();
                background->setCornerColors(cColorf(0.1, 0.1, 0.1), cColorf(0.1, 0.1, 0.1), cColorf(0.0, 0.0, 0.0), cColorf(0.0, 0.0, 0.0));

                labelMessage->setText("Guardando archivo CSV... Espera un momento.");
                labelMessage->m_fontColor.setRed();
                viewport->renderView(framebufferW, framebufferH);
                glfwSwapBuffers(window);
                cSleepMs(50);

                // DIRECTORIO DE GUARDADO: Cambiar esta ruta a la carpeta deseada
                string baseDir = "C:/Users/lpere/OneDrive - UAM/MASTER/TFM/Tareas_Chai3D/Resultados_cognitiva1/";
                string subjectDir = baseDir + subjectID + "/";

                // Generacion dinamica de la estructura de directorios segun el protocolo
                if (isCruzada) {
                    subjectDir += "Cruzada/";
                }
                else {
                    subjectDir += "Normal/";
                }

                if (fatigueType == "practica") {
                    subjectDir += "Practica/";
                }

                try {
                    fs::create_directories(subjectDir);
                }
                catch (const exception& e) {
                    cout << "Error al crear directorios: " << e.what() << endl;
                }

                // Asignacion automatizada de sufijos para evitar sobrescribir archivos
                string suffix = "";
                int counter = 1; 

                string filePrefix = "_" + subjectID + "_";
                if (isCruzada) {
                    filePrefix += "cruzada_";
                }
                filePrefix += fatigueType + "_";

                while (true) {
                    std::ostringstream ss;
                    ss << filePrefix << std::setw(2) << std::setfill('0') << counter;
                    suffix = ss.str(); 
                    string testPath = subjectDir + "datos" + suffix + ".csv";
                    std::ifstream testFile(testPath.c_str());
                    if (!testFile.good()) {
                        break; 
                    }
                    counter++;
                }

                // 1. ARCHIVO DE DATOS CRUDOS: Segmentacion temporal y trayectoria a 1000 Hz
                string exactPathData = subjectDir + "datos" + suffix + ".csv";
                std::ofstream fileData(exactPathData);
                fileData.imbue(std::locale(fileData.getloc(), new CommaFacet()));
                fileData << "Tiempo(s);X;Y;Z;FuerzaX;FuerzaY;FuerzaZ;ID_Ensayo;Fase;En_Aire;Fallos_Aire;Tiempo_Aire(s);Fallo_Cuadrante;Fallos_Pared;Tiempo_Pared(s)\n";
                for (size_t i = 0; i < recordedData.size(); i++) {
                    fileData << recordedData[i].time << ";"
                        << recordedData[i].deviceX << ";"
                        << recordedData[i].deviceY << ";"
                        << recordedData[i].deviceZ << ";"
                        << recordedData[i].forceX << ";"
                        << recordedData[i].forceY << ";"
                        << recordedData[i].forceZ << ";"
                        << recordedData[i].trialID << ";"
                        << recordedData[i].fase << ";"
                        << recordedData[i].inAir << ";"
                        << recordedData[i].currentFailCount << ";"
                        << recordedData[i].currentAirTime << ";"
                        << recordedData[i].isFailingWall << ";"
                        << recordedData[i].currentWallFails << ";"
                        << recordedData[i].currentWallTime << "\n";
                }
                fileData.close();

                // 2. ARCHIVO DE RESUMEN
                string exactPathSummary = subjectDir + "resumen" + suffix + ".csv";
                std::ofstream fileSummary(exactPathSummary);
                double totalExerciseTime = recordedData.empty() ? 0.0 : recordedData.back().time;
                fileSummary.imbue(std::locale(fileSummary.getloc(), new CommaFacet()));
                fileSummary << "Metrica;Valor\n";
                fileSummary << "Tiempo Total Grabacion (s);" << totalExerciseTime << "\n";
                fileSummary << "Total Fallos Aire;" << failCount << "\n";
                fileSummary << "Tiempo Total en Aire (s);" << totalAirTime << "\n";
                fileSummary << "Total Fallos Cuadrante;" << wallFailCount << "\n";
                fileSummary << "Tiempo Total Error Cuadrante (s);" << totalWallTime << "\n";
                fileSummary.close();

                // 3. ARCHIVO DE EVENTOS TEMPORALES
                string exactPathEvents = subjectDir + "eventos" + suffix + ".csv";
                std::ofstream fileEvents(exactPathEvents);
                fileEvents.imbue(std::locale(fileEvents.getloc(), new CommaFacet()));
                fileEvents << "Tipo_Fallo;Numero;Inicio_s;Fin_s\n";
                for (size_t i = 0; i < airErrorEvents.size(); i++) {
                    double eTime = (airErrorEvents[i].endTime == -1.0) ? totalExerciseTime : airErrorEvents[i].endTime;
                    fileEvents << "Aire;" << (i + 1) << ";" << airErrorEvents[i].startTime << ";" << eTime << "\n";
                }
                for (size_t i = 0; i < wallErrorEvents.size(); i++) {
                    double eTime = (wallErrorEvents[i].endTime == -1.0) ? totalExerciseTime : wallErrorEvents[i].endTime;
                    fileEvents << "Cuadrante;" << (i + 1) << ";" << wallErrorEvents[i].startTime << ";" << eTime << "\n";
                }
                fileEvents.close();

                labelMessage->setText("Guardado. Aire: " + cStr(failCount) + "f | Cuadrante: " + cStr(wallFailCount) + "f");
                labelMessage->m_fontColor.setGreen();

                cout << "\n======================================================\n";
                cout << "   TAREA FINALIZADA\n";
                cout << "   Datos guardados en: " << baseDir << "\n";
                cout << "   Archivos generados con sufijo: " << suffix << "\n";
                cout << "======================================================\n";
            }
        }
    }

    world->updateShadowMaps(false, mirroredDisplay);
    viewport->renderView(framebufferW, framebufferH);
    glFinish();
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) cout << "Error: " << gluErrorString(error) << endl;
    glfwSwapBuffers(window);
    freqCounterGraphics.signal(1);
}

//------------------------------------------------------------------------------
void renderHaptics(void)
{
    simulationRunning = true;
    simulationFinished = false;

    while (simulationRunning)
    {
        world->computeGlobalPositions(true);
        tool->updateFromDevice();
        tool->computeInteractionForces();
        tool->applyToDevice();

        if (resetTracking) {
            failCount = 0;
            totalAirTime = 0.0;
            wallFailCount = 0;
            totalWallTime = 0.0;
            airErrorEvents.clear();
            wallErrorEvents.clear();
            wasTouching = (tool->getDeviceGlobalForce().length() > 0.05);
            wasFailingWall = false;
            isFailingWall = false;
            lastRecordTime = recordClock.getCurrentTimeSeconds();
            resetTracking = false;
        }

        if (isRecording) {
            double currentRecordTime = recordClock.getCurrentTimeSeconds();
            double dt = currentRecordTime - lastRecordTime;
            lastRecordTime = currentRecordTime;

            cVector3d dPos = tool->getDeviceGlobalPos();
            bool isTouching = (tool->getDeviceGlobalForce().length() > 0.05);

            // 1. --- Control Propioceptivo (Eje Z) ---
            // Exige un contacto continuo minimo (F > 0.05 N) sobre el plano virtual
            // añadiendo una ligera restriccion fisica de bajo nivel
            if (!isTouching) {
                if (wasTouching) {
                    failCount++;
                    airErrorEvents.push_back({ currentRecordTime, -1.0 });
                }
                totalAirTime += dt;
                isInAir = true;
            }
            else {
                if (!wasTouching && !airErrorEvents.empty() && airErrorEvents.back().endTime == -1.0) {
                    airErrorEvents.back().endTime = currentRecordTime;
                }
                isInAir = false;
            }
            wasTouching = isTouching;

            // 2. --- Segmentacion Temporal y Control Inhibitorio ---
            // Se calcula la distancia al centro para aislar la Fase de Reaccion (procesamiento
            // y toma de decision) de la Fase de Movimiento (ejecucion motora)
            double distToCenter = sqrt(dPos.x() * dPos.x() + dPos.y() * dPos.y());
            bool wrongQuadrantNow = false;

            if (waitingForCenter) {
                currentFase = 0; // Fase de Vuelta
            }
            else {
                if (distToCenter <= START_ZONE_RADIUS) {
                    currentFase = 1; // Fase de Reaccion
                }
                else {
                    currentFase = 2; // Fase de Movimiento

                    // Evaluacion de intrusion en cuadrante (Fallo Inhibitorio):
                    // Se establece un margen de tolerancia transversal de +-0.035 u.v. 
                    // para permitir la variabilidad natural del movimiento
                    double tol = 0.035;
                    if (currentFontColor == 0 && !(dPos.x() < tol && dPos.y() > -tol)) wrongQuadrantNow = true; // Rojo
                    else if (currentFontColor == 1 && !(dPos.x() > -tol && dPos.y() > -tol)) wrongQuadrantNow = true; // Amarillo
                    else if (currentFontColor == 2 && !(dPos.x() < tol && dPos.y() < tol)) wrongQuadrantNow = true; // Azul
                    else if (currentFontColor == 3 && !(dPos.x() > -tol && dPos.y() < tol)) wrongQuadrantNow = true; // Verde
                }
            }

            // 3. --- Actualización del Error Cognitivo (Fallo Inhibitorio) ---
            if (wrongQuadrantNow) {
                if (!wasFailingWall) {
                    wallFailCount++;
                    wallErrorEvents.push_back({ currentRecordTime, -1.0 });
                }
                totalWallTime += dt;
                isFailingWall = true;
            }
            else {
                if (wasFailingWall && !wallErrorEvents.empty() && wallErrorEvents.back().endTime == -1.0) {
                    wallErrorEvents.back().endTime = currentRecordTime;
                }
                isFailingWall = false;
            }
            wasFailingWall = wrongQuadrantNow;

            // 4. --- Grabacion Continua en RAM (1000 Hz) ---
            // Almacenar en memoria durante la ejecucion evita operaciones de disco (I/O)
            // que inducirian lag e inestabilidad en el dispositivo haptico
            RecordData d;
            d.time = currentRecordTime;
            d.deviceX = dPos.x();
            d.deviceY = dPos.y();
            d.deviceZ = dPos.z();

            cVector3d dForce = tool->getDeviceGlobalForce();
            d.forceX = dForce.x();
            d.forceY = dForce.y();
            d.forceZ = dForce.z();

            d.trialID = currentTrialID;
            d.fase = currentFase;

            d.inAir = isInAir;
            d.currentFailCount = failCount;
            d.currentAirTime = totalAirTime;

            d.isFailingWall = isFailingWall;
            d.currentWallFails = wallFailCount;
            d.currentWallTime = totalWallTime;

            recordedData.push_back(d);
        }
        freqCounterHaptics.signal(1);
    }
    simulationFinished = true;
}