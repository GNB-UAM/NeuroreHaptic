//==============================================================================
/*
    Aplicacion haptica para tarea de laberinto con restriccion fisica.
    Desarrollado con CHAI3D para el dispositivo Novint Falcon.

    Objetivo: Registro de cinematica, aplicacion de fuerzas y
    eventos de colision/contacto para el analisis de fatiga motriz.
    Exportacion de datos a alta frecuencia (1000 Hz) en formato CSV.
*/
//==============================================================================

#include "chai3d.h"
#include <GLFW/glfw3.h>
#include <fstream> 
#include <iostream>
#include <vector>
#include <string>
#include <locale>
#include <sstream>
#include <iomanip>
#include <filesystem> 

using namespace chai3d;
using namespace std;
namespace fs = std::filesystem;

// Permite usar la coma como separador decimal en la exportación a CSV
struct CommaFacet : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
};

//------------------------------------------------------------------------------
// ESTADOS DEL JUEGO
//------------------------------------------------------------------------------
enum GameState {
    STATE_MENU,
    STATE_SELECT_FATIGUE, 
    STATE_WAITING_FOR_START,
    STATE_PLAYING,
    STATE_FINISHED
};

GameState currentState = STATE_MENU;
double selectedFriction = 0.0;
string fatigueType = ""; // Guarda el tipo de sesion: "pre", "post" o "practica"
string subjectID = "";
bool isCruzada = false;

//------------------------------------------------------------------------------
// VARIABLES DE GRABACION CSV Y BOTON
//------------------------------------------------------------------------------
struct RecordData {
    double time;
    double deviceX, deviceY, deviceZ;
    double forceX, forceY, forceZ;          // Registro de fuerza ejercida
    bool inAir;                             // 1 si esta en el aire, 0 si esta tocando
    int currentFailCount;                   // Fallos en aire acumulados hasta ese momento
    double currentAirTime;                  // Tiempo en aire acumulado hasta ese momento
    bool touchingWall;                      // 1 si esta tocando la pared, 0 si no
    int currentWallFails;                   // Fallos de pared acumulados
    double currentWallTime;                 // Tiempo en pared acumulado
};

std::vector<RecordData> recordedData;
volatile bool isRecording = false;
cPrecisionClock recordClock;
bool lastButtonState = false;

// Estructuras para guardar los intervalos exactos de los errores
struct ErrorEvent {
    double startTime;
    double endTime;
};
std::vector<ErrorEvent> airErrorEvents;
std::vector<ErrorEvent> wallErrorEvents;
 

//------------------------------------------------------------------------------
// VARIABLES DE LOGICA DE SEGUIMIENTO (CONTACTO Y ERRORES)
//------------------------------------------------------------------------------
// Variables de Aire
volatile bool isInAir = false;
volatile int failCount = 0;
volatile double totalAirTime = 0.0;
bool wasTouching = true;

volatile bool isPushingTooHard = false;

// Variables de Pared
volatile bool isTouchingWall = false;
volatile int wallFailCount = 0;
volatile double totalWallTime = 0.0;
bool wasTouchingWall = false;

volatile bool resetTracking = false;
volatile bool hasReachedGoal = false;
volatile bool hasReachedMidpoint = false;
double lastRecordTime = 0.0;

//------------------------------------------------------------------------------
// GENERAL SETTINGS
//------------------------------------------------------------------------------
cStereoMode stereoMode = C_STEREO_DISABLED;
bool fullscreen = false;
bool mirroredDisplay = false;

//------------------------------------------------------------------------------
// DECLARED VARIABLES
//------------------------------------------------------------------------------
cWorld* world;
cCamera* camera;
cViewport* viewport = nullptr;
cSpotLight* light;
cHapticDeviceHandler* handler;
cGenericHapticDevicePtr hapticDevice;
cToolCursor* tool;

// Objetos del entorno
cMesh* workspacePlane;
std::vector<cMesh*> walls;

// Estructura para limites fisicos de paredes
struct WallBounds {
    double minX, maxX;
    double minY, maxY;
};
std::vector<WallBounds> wallsAABB;

cShapeSphere* startSphere;
cShapeSphere* endSphere;

// Interfaz de Usuario (UI)
cFontPtr font;
cFontPtr fontLarge;
cLabel* labelRates;
cLabel* labelMenuTitle;
cLabel* labelInstructions;
cLabel* labelBtnNormal;
cLabel* labelBtnMedium;
cLabel* labelBtnHigh;

// Menu de configuracion de Fatiga
cLabel* labelFatigueTitle;
cLabel* labelBtnPre;
cLabel* labelBtnPost;
cLabel* labelBtnPractica;
// --------------------------------------------------------

cLabel* labelMessage;
cLabel* labelBtnBack;
cBackground* background;

// Control de simulacion
bool simulationRunning = false;
bool simulationFinished = true;
cFrequencyCounter freqCounterGraphics;
cFrequencyCounter freqCounterHaptics;
cThread* hapticsThread;

// Ventana
GLFWwindow* window = nullptr;
int windowW = 0;
int windowH = 0;
int framebufferW = 0;
int framebufferH = 0;
int swapInterval = 1;

// Propiedades hapticas globales
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
void buildPath();
void showFatigueMenu(); 
void startGame();
void resetToMenu();

bool hitButton(cLabel* btn, double x, double y) {
    if (!btn->getShowEnabled()) return false;
    double bx = btn->getLocalPos().x();
    double by = btn->getLocalPos().y();
    double bw = btn->getWidth();
    double bh = btn->getHeight();
    return (x >= bx && x <= bx + bw && y >= by && y <= by + bh);
}

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

    window = glfwCreateWindow(windowW, windowH, "Chai3D - Path Following Task", NULL, NULL);
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

    // Inicializacion segura de extensiones OpenGL dependiendo de la plataforma
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

    // Ajuste de la rigidez maxima en funcion de la escala del entorno virtual 
    // Es crucial para mantener la estabilidad mecanica del Novint Falcon y evitar vibraciones
    double workspaceScaleFactor = tool->getWorkspaceScaleFactor();
    maxStiffness = hapticDeviceInfo.m_maxLinearStiffness / workspaceScaleFactor;

    string currentpath = cGetCurrentPath();
    bool fileload;

    workspacePlane = new cMesh();
    cCreatePlane(workspacePlane, 1.4, 1.4);
    workspacePlane->createAABBCollisionDetector(toolRadius);
    world->addChild(workspacePlane);

    workspacePlane->setLocalPos(0.0, 0.0, 0.0);

    workspacePlane->m_texture = cTexture2d::create();
    fileload = workspacePlane->m_texture->loadFromFile(currentpath + "../resources/images/sand.jpg");

    if (!fileload) {
        cout << "Aviso: No se pudo cargar la imagen de textura (sand.jpg). El suelo sera gris solido." << endl;
        workspacePlane->setUseTexture(false);
        workspacePlane->m_material->setGrayLight();
    }
    else {
        workspacePlane->setUseTexture(true);
        workspacePlane->m_material->setWhite();
        cNormalMapPtr normalMap = cNormalMap::create();
        normalMap->createMap(workspacePlane->m_texture);
        workspacePlane->m_normalMap = normalMap;
    }

    workspacePlane->m_material->setHapticTriangleSides(true, false);

    // Construccion del laberinto
    buildPath();

    // Ocultamos todos los elementos inicialmente (estado menu)
    workspacePlane->setShowEnabled(false);
    for (size_t i = 0; i < walls.size(); i++) {
        walls[i]->setShowEnabled(false);
    }
    startSphere->setShowEnabled(false);
    endSphere->setShowEnabled(false);

    font = NEW_CFONT_CALIBRI_20();
    fontLarge = NEW_CFONT_CALIBRI_32();
    cFontPtr fontInstrucciones = NEW_CFONT_CALIBRI_24();

    labelRates = new cLabel(font);
    camera->m_frontLayer->addChild(labelRates);

    labelMenuTitle = new cLabel(fontLarge);
    labelMenuTitle->setText("SELECCIONA LA FRICCION DEL EJERCICIO:");
    labelMenuTitle->m_fontColor.setWhite();
    camera->m_frontLayer->addChild(labelMenuTitle);

    labelBtnNormal = new cLabel(fontLarge);
    labelBtnNormal->setText("[ NORMAL ]");
    labelBtnNormal->m_fontColor.setGreen();
    camera->m_frontLayer->addChild(labelBtnNormal);

    labelBtnMedium = new cLabel(fontLarge);
    labelBtnMedium->setText("[ MEDIA ]");
    labelBtnMedium->m_fontColor.setYellow();
    camera->m_frontLayer->addChild(labelBtnMedium);

    labelBtnHigh = new cLabel(fontLarge);
    labelBtnHigh->setText("[ ALTA ]");
    labelBtnHigh->m_fontColor.setRed();
    camera->m_frontLayer->addChild(labelBtnHigh);

    // Elementos UI: Menu de seleccion de tipo de registro (Pre-Fatiga / Post-Fatiga / Practica)
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

    labelInstructions = new cLabel(fontInstrucciones);
    labelInstructions->setText("En el siguiente juego primero tiene que situarse en la bola verde y sobre el terreno de friccion para poder comenzar. \n Una vez situado presione el boton central del robot o la barra espaciadora para indicar que ya esta listo \n y comience a seguir el camino inmediatamente. Si en algun momento se despega de la superficie la bola y el fondo se pondran \n rojo indicando un fallo (mantengase constantemente pegado a la superficie).");
    labelInstructions->m_fontColor.setWhite();
    camera->m_frontLayer->addChild(labelInstructions);

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

    background = new cBackground();
    camera->m_backLayer->addChild(background);
    background->setCornerColors(cColorf(0.1, 0.1, 0.1), cColorf(0.1, 0.1, 0.1), cColorf(0.0, 0.0, 0.0), cColorf(0.0, 0.0, 0.0));

    viewport = new cViewport(camera);

    // Separacion del bucle haptico (1000 Hz) del grafico para garantizar
    // un feedback de fuerza estable y registrar metricas sin perdida de paquetes
    hapticsThread = new cThread();
    hapticsThread->start(renderHaptics, CTHREAD_PRIORITY_HAPTICS);
    atexit(close);

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
void addWall(double cx, double cy, double sx, double sy, double toolRad) {
    double wallHeight = 0.15;
    double zCenter = wallHeight / 2.0;

    cMesh* wall = new cMesh();
    cCreateBox(wall, sx, sy, wallHeight);
    wall->setLocalPos(cx, cy, zCenter);

    wall->m_material->setBlue();
    wall->m_material->setUseHapticShading(true);

    wall->createAABBCollisionDetector(toolRad);
    world->addChild(wall);

    walls.push_back(wall);

    // Calculo manual de los lamites de colision(AABB) con margen ajustado al radio del cursor.
    // Esto permite contabilizar de forma independiente los choques (errores espaciales) 
    // sin depender exclusivamente del motor de renderizado haptico
    WallBounds bounds;
    double margin = toolRad - 0.001;
    bounds.minX = cx - (sx / 2.0) - margin;
    bounds.maxX = cx + (sx / 2.0) + margin;
    bounds.minY = cy - (sy / 2.0) - margin;
    bounds.maxY = cy + (sy / 2.0) + margin;

    wallsAABB.push_back(bounds);
}

//------------------------------------------------------------------------------
void buildPath()
{
    double wallThickness = 0.01;

    addWall(-0.42, 0.0, wallThickness, 0.64, toolRadius);
    addWall(0.42, 0.0, wallThickness, 0.64, toolRadius);
    addWall(0.0, 0.30, 0.86, wallThickness, toolRadius);
    addWall(0.06, -0.30, 0.74, wallThickness, toolRadius);

    double innerWallLength = 0.50;
    double topAttachY = 0.06;
    double botAttachY = -0.06;

    addWall(-0.30, botAttachY, wallThickness, innerWallLength, toolRadius);
    addWall(-0.18, topAttachY, wallThickness, innerWallLength, toolRadius);
    addWall(-0.06, botAttachY, wallThickness, innerWallLength, toolRadius);
    addWall(0.06, topAttachY, wallThickness, innerWallLength, toolRadius);
    addWall(0.18, botAttachY, wallThickness, innerWallLength, toolRadius);
    addWall(0.30, topAttachY, wallThickness, innerWallLength, toolRadius);

    startSphere = new cShapeSphere(0.012);
    world->addChild(startSphere);

    startSphere->setLocalPos(-0.39, -0.32, 0.005);
    startSphere->m_material->setGreen();
    startSphere->m_material->setUseHapticShading(true);
    startSphere->setHapticEnabled(false);

    endSphere = new cShapeSphere(0.015);
    world->addChild(endSphere);
    endSphere->setLocalPos(0.39, 0.24, 0.0);
    endSphere->m_material->setRed();
    endSphere->m_material->setUseHapticShading(true);
}

//------------------------------------------------------------------------------
void resetToMenu()
{
    isRecording = false;
    recordClock.stop();

    resetTracking = true;
    hasReachedGoal = false;
    hasReachedMidpoint = false;
    isInAir = false;
    isTouchingWall = false;
    isPushingTooHard = false;

    // Limpiar los vectores de errores al resetear
    airErrorEvents.clear();
    wallErrorEvents.clear();

    workspacePlane->setShowEnabled(false);
    for (size_t i = 0; i < walls.size(); i++) {
        walls[i]->setShowEnabled(false);
    }
    startSphere->setShowEnabled(false);
    endSphere->setShowEnabled(false);
    labelMessage->setShowEnabled(false);
    labelBtnBack->setShowEnabled(false);

    startSphere->m_material->setGreen();
    tool->getHapticPoint(0)->m_sphereProxy->m_material->setWhite();
    background->setCornerColors(cColorf(0.1, 0.1, 0.1), cColorf(0.1, 0.1, 0.1), cColorf(0.0, 0.0, 0.0), cColorf(0.0, 0.0, 0.0));
    for (size_t i = 0; i < walls.size(); i++) walls[i]->m_material->setBlue();

    // Restablecer el primer menu
    labelMenuTitle->setShowEnabled(true);
    labelInstructions->setShowEnabled(true);
    labelBtnNormal->setShowEnabled(true);
    labelBtnMedium->setShowEnabled(true);
    labelBtnHigh->setShowEnabled(true);

    // Ocultar los botones del menu de fatiga
    labelFatigueTitle->setShowEnabled(false);
    labelBtnPre->setShowEnabled(false);
    labelBtnPost->setShowEnabled(false);
    labelBtnPractica->setShowEnabled(false);
    fatigueType = ""; 
    // ----------------------------------------------------------------

    currentState = STATE_MENU;
}

//------------------------------------------------------------------------------
void showFatigueMenu()
{
    currentState = STATE_SELECT_FATIGUE;

    // Ocultar botones de friccion
    labelMenuTitle->setShowEnabled(false);
    labelInstructions->setShowEnabled(false);
    labelBtnNormal->setShowEnabled(false);
    labelBtnMedium->setShowEnabled(false);
    labelBtnHigh->setShowEnabled(false);

    // Mostrar botones de fatiga
    labelFatigueTitle->setShowEnabled(true);
    labelBtnPre->setShowEnabled(true);
    labelBtnPost->setShowEnabled(true);
    labelBtnPractica->setShowEnabled(true);

    labelBtnBack->setShowEnabled(true);
}

//------------------------------------------------------------------------------
void startGame()
{
    currentState = STATE_WAITING_FOR_START;

    // Ocultar el menu de seleccion de fatiga
    labelFatigueTitle->setShowEnabled(false);
    labelBtnPre->setShowEnabled(false);
    labelBtnPost->setShowEnabled(false);
    labelBtnPractica->setShowEnabled(false);
    // -------------------------------------------------------

    workspacePlane->setShowEnabled(true);
    for (size_t i = 0; i < walls.size(); i++) {
        walls[i]->setShowEnabled(true);
    }
    startSphere->setShowEnabled(true);
    endSphere->setShowEnabled(true);

    labelBtnBack->setShowEnabled(true);

    labelMessage->setText("Acomodate y presiona el boton central (O LA BARRA ESPACIADORA).");
    labelMessage->setShowEnabled(true);

    double stiffness = 0.5 * maxStiffness;

    // Aplicacion de la friccion dinamica y estatica sobre el plano de trabajo.
    // Esta friccion actua como elemento resistivo principal, obligando al usuario 
    // a ejercer un reclutamiento muscular continuo
    workspacePlane->m_material->setStiffness(stiffness);
    workspacePlane->m_material->setStaticFriction(selectedFriction);
    workspacePlane->m_material->setDynamicFriction(selectedFriction);

    if (workspacePlane->getUseTexture()) {
        workspacePlane->m_material->setTextureLevel(0.6);
    }

    for (size_t i = 0; i < walls.size(); i++) {
        walls[i]->m_material->setStiffness(stiffness);
        walls[i]->m_material->setStaticFriction(selectedFriction);
        walls[i]->m_material->setDynamicFriction(selectedFriction);
    }

    endSphere->m_material->setStiffness(stiffness);
    endSphere->m_material->setStaticFriction(selectedFriction);
    endSphere->m_material->setDynamicFriction(selectedFriction);
}

//------------------------------------------------------------------------------
void onMouseButtonCallback(GLFWwindow* a_window, int a_button, int a_action, int a_mods)
{
    if (a_button == GLFW_MOUSE_BUTTON_LEFT && a_action == GLFW_PRESS)
    {
        double xpos, ypos;
        glfwGetCursorPos(a_window, &xpos, &ypos);
        double chaiY = windowH - ypos;

        if (currentState == STATE_MENU)
        {
            // Transicion: Seleccion de friccion -> Menu de fatiga
            if (hitButton(labelBtnNormal, xpos, chaiY)) { selectedFriction = 0.1; showFatigueMenu(); }
            else if (hitButton(labelBtnMedium, xpos, chaiY)) { selectedFriction = 0.25; showFatigueMenu(); }
            else if (hitButton(labelBtnHigh, xpos, chaiY)) { selectedFriction = 0.40; showFatigueMenu(); }
        }
        else if (currentState == STATE_SELECT_FATIGUE)
        {
            // Transicion: Selección de tipo de sesion -> Inicio del juego
            if (hitButton(labelBtnPractica, xpos, chaiY)) { fatigueType = "practica"; startGame(); } 
            else if (hitButton(labelBtnPre, xpos, chaiY)) { fatigueType = "pre"; startGame(); }
            else if (hitButton(labelBtnPost, xpos, chaiY)) { fatigueType = "post"; startGame(); }
            else if (hitButton(labelBtnBack, xpos, chaiY)) { resetToMenu(); }
        }
        else if (currentState == STATE_WAITING_FOR_START || currentState == STATE_PLAYING || currentState == STATE_FINISHED)
        {
            if (hitButton(labelBtnBack, xpos, chaiY)) {
                resetToMenu();
            }
        }
    }
}

void onWindowSizeCallback(GLFWwindow* a_window, int a_width, int a_height) { windowW = a_width; windowH = a_height; }
void onFrameBufferSizeCallback(GLFWwindow* a_window, int a_width, int a_height) { framebufferW = a_width; framebufferH = a_height; }
void onWindowContentScaleCallback(GLFWwindow* a_window, float a_xscale, float a_yscale) { if (viewport) viewport->setContentScale(a_xscale, a_yscale); }
void onErrorCallback(int a_error, const char* a_description) { cout << "Error: " << a_description << endl; }

void onKeyCallback(GLFWwindow* a_window, int a_key, int a_scancode, int a_action, int a_mods)
{
    if ((a_action != GLFW_PRESS) && (a_action != GLFW_REPEAT)) return;
    if ((a_key == GLFW_KEY_ESCAPE) || (a_key == GLFW_KEY_Q)) glfwSetWindowShouldClose(a_window, GLFW_TRUE);
}

void close(void)
{
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

    labelRates->setText(cStr(freqCounterGraphics.getFrequency(), 0) + " Hz / " +
        cStr(freqCounterHaptics.getFrequency(), 0) + " Hz");
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

    if (currentState == STATE_MENU) {
        labelMenuTitle->setLocalPos((displayW - labelMenuTitle->getWidth()) / 2, displayH * 0.85);
        labelInstructions->setLocalPos((displayW - labelInstructions->getWidth()) / 2, displayH * 0.60);
        labelBtnNormal->setLocalPos((displayW - labelBtnNormal->getWidth()) / 2, displayH * 0.40);
        labelBtnMedium->setLocalPos((displayW - labelBtnMedium->getWidth()) / 2, displayH * 0.30);
        labelBtnHigh->setLocalPos((displayW - labelBtnHigh->getWidth()) / 2, displayH * 0.20);
    }
    
    else if (currentState == STATE_SELECT_FATIGUE) {
        labelFatigueTitle->setLocalPos((displayW - labelFatigueTitle->getWidth()) / 2, displayH * 0.70);
        labelBtnPractica->setLocalPos((displayW - labelBtnPractica->getWidth()) / 2, displayH * 0.50);
        labelBtnPre->setLocalPos((displayW - labelBtnPre->getWidth()) / 2, displayH * 0.40);
        labelBtnPost->setLocalPos((displayW - labelBtnPost->getWidth()) / 2, displayH * 0.30);

        if (labelBtnBack->getShowEnabled()) {
            labelBtnBack->setLocalPos(20, displayH - 50);
        }
    }
    // -----------------------------------------------------------------------------
    else {

        if (labelBtnBack->getShowEnabled()) {
            labelBtnBack->setLocalPos(20, displayH - 50);
        }

        if (labelMessage->getShowEnabled()) {
            labelMessage->setLocalPos((displayW - labelMessage->getWidth()) / 2, displayH * 0.85);
        }

        if (currentState == STATE_WAITING_FOR_START) {

            bool isTouchingPreStart = (tool->getDeviceGlobalForce().length() > 0.1);
            if (isTouchingPreStart) {
                background->setCornerColors(cColorf(0.0, 0.4, 0.0), cColorf(0.0, 0.4, 0.0), cColorf(0.0, 0.1, 0.0), cColorf(0.0, 0.1, 0.0));
            }
            else {
                background->setCornerColors(cColorf(0.4, 0.0, 0.0), cColorf(0.4, 0.0, 0.0), cColorf(0.1, 0.0, 0.0), cColorf(0.1, 0.0, 0.0));
            }

            if (buttonClicked) {
                currentState = STATE_PLAYING;
                startSphere->m_material->setGray();
                labelMessage->setText("GRABANDO. Recorre el laberinto hasta la bola roja.");
                labelMessage->m_fontColor.setYellow();

                background->setCornerColors(cColorf(0.1, 0.1, 0.1), cColorf(0.1, 0.1, 0.1), cColorf(0.0, 0.0, 0.0), cColorf(0.0, 0.0, 0.0));

                recordedData.clear();
                recordedData.reserve(180000);

                airErrorEvents.clear();
                wallErrorEvents.clear();

                cout << "\n======================================================\n";
                cout << "   GRABACION INICIADA (GUARDANDO EN RAM)\n";
                cout << "======================================================\n";

                recordClock.reset();
                recordClock.start();

                resetTracking = true;
                isRecording = true;
            }
        }

        // Feedback visual en tiempo real para el control propioceptivo:
        // - Rojo: Perdida de contacto (Fz < 0.1 N). Se contabiliza como error
        // - Amarillo: Exceso de presion (Fz > 10.5 N) (posibilidad de analisis de modulacion de fuerza)
        else if (currentState == STATE_PLAYING) {

            if (isInAir) {
                tool->getHapticPoint(0)->m_sphereProxy->m_material->setRed();
                background->setCornerColors(cColorf(0.4, 0.0, 0.0), cColorf(0.4, 0.0, 0.0), cColorf(0.1, 0.0, 0.0), cColorf(0.1, 0.0, 0.0));
            }
            else if (isPushingTooHard) {
                tool->getHapticPoint(0)->m_sphereProxy->m_material->setYellow(); 
                background->setCornerColors(cColorf(0.4, 0.4, 0.0), cColorf(0.4, 0.4, 0.0), cColorf(0.1, 0.1, 0.0), cColorf(0.1, 0.1, 0.0)); 
            }
            else {
                tool->getHapticPoint(0)->m_sphereProxy->m_material->setWhite();
                background->setCornerColors(cColorf(0.1, 0.1, 0.1), cColorf(0.1, 0.1, 0.1), cColorf(0.0, 0.0, 0.0), cColorf(0.0, 0.0, 0.0));
            }

            if (isTouchingWall) {
                for (size_t i = 0; i < walls.size(); i++) walls[i]->m_material->setRed();
            }
            else {
                for (size_t i = 0; i < walls.size(); i++) walls[i]->m_material->setBlue();
            }

            if (hasReachedMidpoint) {
                endSphere->m_material->setGray();
                startSphere->m_material->setRed();
                labelMessage->setText("VUELVE AL PRINCIPIO (Bola en ROJO)");
            }

            if (hasReachedGoal || buttonClicked) {
                currentState = STATE_FINISHED;
                isRecording = false;
                recordClock.stop();

                tool->getHapticPoint(0)->m_sphereProxy->m_material->setWhite();
                background->setCornerColors(cColorf(0.1, 0.1, 0.1), cColorf(0.1, 0.1, 0.1), cColorf(0.0, 0.0, 0.0), cColorf(0.0, 0.0, 0.0));
                for (size_t i = 0; i < walls.size(); i++) walls[i]->m_material->setBlue();

                labelMessage->setText("Guardando archivo CSV... Por favor espera un momento.");
                labelMessage->m_fontColor.setRed();
                viewport->renderView(framebufferW, framebufferH);
                glfwSwapBuffers(window);

                cSleepMs(50);

				// DIRECTORIO DE GUARDADO: Cambiar esta ruta a la carpeta deseada
                string baseDir = "C:/Users/lpere/OneDrive - UAM/MASTER/TFM/Tareas_Chai3D/Resultados_fisica1/";
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
                // ----------------------------------------------------------------------------------------

                // 1. ARCHIVO DE DATOS CRUDOS: Serie temporal completa a 1000 Hz (Cinematica y Dinamica)
                string exactPathData = subjectDir + "datos" + suffix + ".csv";
                std::ofstream fileData(exactPathData);

                fileData.imbue(std::locale(fileData.getloc(), new CommaFacet()));

                fileData << "Tiempo(s);X;Y;Z;FuerzaX;FuerzaY;FuerzaZ;En_Aire;Fallos_Aire;Tiempo_Aire(s);En_Pared;Fallos_Pared;Tiempo_Pared(s)\n";
                for (size_t i = 0; i < recordedData.size(); i++) {
                    fileData << recordedData[i].time << ";"
                        << recordedData[i].deviceX << ";"
                        << recordedData[i].deviceY << ";"
                        << recordedData[i].deviceZ << ";"
                        << recordedData[i].forceX << ";"
                        << recordedData[i].forceY << ";"
                        << recordedData[i].forceZ << ";"
                        << recordedData[i].inAir << ";"
                        << recordedData[i].currentFailCount << ";"
                        << recordedData[i].currentAirTime << ";"
                        << recordedData[i].touchingWall << ";"
                        << recordedData[i].currentWallFails << ";"
                        << recordedData[i].currentWallTime << "\n";
                }
                fileData.close();

                // 2. ARCHIVO DE RESUMEN: Metricas globales de la sesion para evaluacion rapida
                string exactPathSummary = subjectDir + "resumen" + suffix + ".csv";
                std::ofstream fileSummary(exactPathSummary);

                double totalExerciseTime = recordedData.empty() ? 0.0 : recordedData.back().time;

                fileSummary.imbue(std::locale(fileSummary.getloc(), new CommaFacet()));

                fileSummary << "Metrica;Valor\n";
                fileSummary << "Tiempo Total Ejercicio (s);" << totalExerciseTime << "\n";
                fileSummary << "Total Fallos Aire;" << failCount << "\n";
                fileSummary << "Tiempo Total en Aire (s);" << totalAirTime << "\n";
                fileSummary << "Total Choques Pared;" << wallFailCount << "\n";
                fileSummary << "Tiempo Total Paredes (s);" << totalWallTime << "\n";

                fileSummary.close();

                // 3. ARCHIVO DE EVENTOS TEMPORALES: Inicio y fin de errores para analisis de tiempos de reaccion/correccion
                string exactPathEvents = subjectDir + "eventos" + suffix + ".csv";
                std::ofstream fileEvents(exactPathEvents);

                fileEvents.imbue(std::locale(fileEvents.getloc(), new CommaFacet()));
                fileEvents << "Tipo_Fallo;Numero;Inicio_s;Fin_s\n";

                // Volcar eventos de aire
                for (size_t i = 0; i < airErrorEvents.size(); i++) {
                    double eTime = (airErrorEvents[i].endTime == -1.0) ? totalExerciseTime : airErrorEvents[i].endTime;
                    fileEvents << "Aire;" << (i + 1) << ";" << airErrorEvents[i].startTime << ";" << eTime << "\n";
                }

                // Volcar eventos de pared
                for (size_t i = 0; i < wallErrorEvents.size(); i++) {
                    double eTime = (wallErrorEvents[i].endTime == -1.0) ? totalExerciseTime : wallErrorEvents[i].endTime;
                    fileEvents << "Pared;" << (i + 1) << ";" << wallErrorEvents[i].startTime << ";" << eTime << "\n";
                }

                fileEvents.close();

                // 4. ACTUALIZAR MENSAJES EN PANTALLA Y CONSOLA
                string resultMsg = "Aire: " + cStr(failCount) + " f (" + cStr(totalAirTime, 1) + "s) | Pared: " + cStr(wallFailCount) + " f (" + cStr(totalWallTime, 1) + "s)";
                labelMessage->setText(resultMsg);
                labelMessage->m_fontColor.setGreen();

                cout << "\n======================================================\n";
                if (hasReachedGoal) {
                    cout << "   META ALCANZADA. EJERCICIO FINALIZADO CON EXITO\n";
                }
                else {
                    cout << "   GRABACION FINALIZADA MANUALMENTE\n";
                }
                cout << "   Total de datos capturados: " << recordedData.size() << "\n";
                cout << "   Datos raw guardados en: " << exactPathData << "\n";
                cout << "   Resumen guardado en: " << exactPathSummary << "\n";
                cout << "   Eventos guardados en: " << exactPathEvents << "\n";
                cout << "------------------------------------------------------\n";
                cout << "   RESULTADOS DE CONTACTO CON SUELO (AIRE):\n";
                cout << "   Veces que se solto la superficie: " << failCount << "\n";
                cout << "   Tiempo total sin contacto: " << totalAirTime << " segundos\n";
                cout << "------------------------------------------------------\n";
                cout << "   RESULTADOS DE CONTACTO CON PAREDES:\n";
                cout << "   Veces que choco contra paredes: " << wallFailCount << "\n";
                cout << "   Tiempo total chocando: " << totalWallTime << " segundos\n";
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
            hasReachedGoal = false;
            hasReachedMidpoint = false;

            airErrorEvents.clear();
            wallErrorEvents.clear();

            wasTouching = (tool->getDeviceGlobalForce().length() > 0.1);
            wasTouchingWall = false;

            lastRecordTime = recordClock.getCurrentTimeSeconds();
            resetTracking = false;
        }

        if (isRecording) {
            double currentRecordTime = recordClock.getCurrentTimeSeconds();
            double dt = currentRecordTime - lastRecordTime;
            lastRecordTime = currentRecordTime;

            // 1. --- Control Propioceptivo (Eje Z) ---
            // Evalua la capacidad del usuario para mantener una fuerza estable
            // F < 0.1 N se registra como perdida de contacto y F >= 10.5 N como exceso de fuerza
            double currentForce = tool->getDeviceGlobalForce().length();
            bool isTouching = (currentForce > 0.1);
            isPushingTooHard = (currentForce >= 10.5);

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


            // 2. --- Precision Espacial y Colisiones (Ejes X e Y) ---
            // Eficiencia y precision de la trayectoria comprobando intersecciones AABB
            // para cuantificar los choques producidos por falta de control direccional
            bool hittingWallNow = false;
            cVector3d dPos = tool->getDeviceGlobalPos();

            for (size_t i = 0; i < wallsAABB.size(); i++) {
                if (dPos.x() > wallsAABB[i].minX && dPos.x() < wallsAABB[i].maxX &&
                    dPos.y() > wallsAABB[i].minY && dPos.y() < wallsAABB[i].maxY &&
                    dPos.z() < 0.15)
                {
                    hittingWallNow = true;
                    break;
                }
            }

            if (hittingWallNow) {
                if (!wasTouchingWall) {
                    wallFailCount++;
                    wallErrorEvents.push_back({ currentRecordTime, -1.0 });
                }
                totalWallTime += dt;
                isTouchingWall = true;
            }
            else {
                if (wasTouchingWall && !wallErrorEvents.empty() && wallErrorEvents.back().endTime == -1.0) {
                    wallErrorEvents.back().endTime = currentRecordTime;
                }
                isTouchingWall = false;
            }
            wasTouchingWall = hittingWallNow;


            // 3. --- Deteccion de Meta (Recorrido de Ida y Vuelta) ---
            // Exigir el regreso al inicio permite capturar datos simetricos y prolongar 
            // la sesion para obtener una serie temporal lo suficientemente robusta
            cVector3d redPos = endSphere->getLocalPos();
            cVector3d greenPos = startSphere->getLocalPos();

            double distToRed = sqrt(pow(dPos.x() - redPos.x(), 2) + pow(dPos.y() - redPos.y(), 2));
            double distToGreen = sqrt(pow(dPos.x() - greenPos.x(), 2) + pow(dPos.y() - greenPos.y(), 2));

            if (!hasReachedMidpoint) {
                // FASE DE IDA: a la bola roja
                if (distToRed <= 0.021) {
                    hasReachedMidpoint = true;
                }
            }
            else {
                // FASE DE VUELTA: a la bola verde
                if (distToGreen <= 0.021) {
                    hasReachedGoal = true;
                }
            }


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

            d.inAir = isInAir;
            d.currentFailCount = failCount;
            d.currentAirTime = totalAirTime;

            d.touchingWall = isTouchingWall;
            d.currentWallFails = wallFailCount;
            d.currentWallTime = totalWallTime;

            recordedData.push_back(d);
        }

        freqCounterHaptics.signal(1);
    }

    simulationFinished = true;
}