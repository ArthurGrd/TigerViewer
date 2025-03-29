#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <array>
#include <memory>
#include <stdexcept>
#include <unistd.h>
#include <sys/wait.h>
#include <algorithm>
#include <vector>

// TinyFileDialogs pour l'explorateur de fichier
#include "tinyfiledialogs.h"

// OpenGL / GLFW
#include <GL/glew.h>
#include <GLFW/glfw3.h>

// Dear ImGui et ses backends
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// librsvg, cairo, glib
#include <librsvg/rsvg.h>
#include <cairo.h>
#include <glib.h>

// =============================
// Variables globales pour dimensions de base du SVG
// =============================
static int baseSVGWidth = 0;
static int baseSVGHeight = 0;

// =============================
// Constantes
// =============================
static constexpr float COMPILE_DELAY = 0.5f; // délai auto-compilation après modification

// =============================
// Structure pour capturer la sortie du compilateur (stdout et stderr)
struct CompilerOutput {
    std::string out; // sortie standard (DOT)
    std::string err; // sortie d'erreur
};

// =============================
// Fonction utilitaire : exécute "./tc [options] --ast-dump -" en lui passant input via stdin
// Les options sont ajoutées dans l'ordre : -X, -bB, --rename, -eE.
CompilerOutput run_compiler_with_input(const std::string &input,
                                         bool flagX, bool flagB, bool flagR, bool flagE)
{
    int in_pipe[2], out_pipe[2], err_pipe[2];
    if (pipe(in_pipe) == -1)
        throw std::runtime_error("Erreur création pipe d'entrée.");
    if (pipe(out_pipe) == -1) {
        close(in_pipe[0]); close(in_pipe[1]);
        throw std::runtime_error("Erreur création pipe de sortie.");
    }
    if (pipe(err_pipe) == -1) {
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        throw std::runtime_error("Erreur création pipe d'erreur.");
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        throw std::runtime_error("Erreur lors du fork.");
    }
    else if (pid == 0) {
        // Enfant
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);

        std::vector<char*> args;
        args.push_back(const_cast<char*>("./tc"));
        if (flagX)
            args.push_back(const_cast<char*>("-X"));
        if (flagB)
            args.push_back(const_cast<char*>("-bB"));
        if (flagR)
            args.push_back(const_cast<char*>("--rename"));
        if (flagE)
            args.push_back(const_cast<char*>("-eE"));
        args.push_back(const_cast<char*>("--ast-dump"));
        args.push_back(const_cast<char*>("-"));
        args.push_back(nullptr);

        execv("./tc", args.data());
        perror("execv échoué");
        _exit(1);
    }
    else {
        // Parent
        close(in_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[1]);

        ssize_t total_written = 0;
        ssize_t to_write = input.size();
        const char* data = input.c_str();
        while (total_written < to_write) {
            ssize_t written = write(in_pipe[1], data + total_written, to_write - total_written);
            if (written < 0) {
                close(in_pipe[1]); close(out_pipe[0]); close(err_pipe[0]);
                throw std::runtime_error("Erreur écriture pipe.");
            }
            total_written += written;
        }
        close(in_pipe[1]);

        CompilerOutput compOut;
        {
            char buffer[128];
            ssize_t n;
            while ((n = read(out_pipe[0], buffer, sizeof(buffer))) > 0) {
                compOut.out.append(buffer, n);
            }
            close(out_pipe[0]);
        }
        {
            char buffer[128];
            ssize_t n;
            while ((n = read(err_pipe[0], buffer, sizeof(buffer))) > 0) {
                compOut.err.append(buffer, n);
            }
            close(err_pipe[0]);
        }

        int status;
        waitpid(pid, &status, 0);
        return compOut;
    }
}

// Écrit le DOT dans /tmp/ast.dot et appelle Graphviz pour générer /tmp/ast_new.svg.
// Si Graphviz réussit, remplace /tmp/ast.svg par le nouveau SVG.
bool generate_svg_from_dot(const std::string &dotContent)
{
    if (dotContent.empty())
        return false;
    const std::string dotPath = "/tmp/ast.dot";
    {
        std::ofstream dotFile(dotPath);
        dotFile << dotContent;
    }
    std::string cmd = "dot -Tsvg " + dotPath + " -o /tmp/ast_new.svg";
    int ret = std::system(cmd.c_str());
    if (ret != 0)
        return false;
    std::system("mv /tmp/ast_new.svg /tmp/ast.svg");
    return true;
}

// Rend le SVG via librsvg et Cairo selon le zoom et retourne une texture OpenGL.
// Met à jour outWidth et outHeight, et enregistre les dimensions de base dans baseSVGWidth/baseSVGHeight.
GLuint render_svg_with_librsvg(const std::string &svgFilePath, float zoom, int &outWidth, int &outHeight)
{
    GError *error = nullptr;
    RsvgHandle *handle = rsvg_handle_new_from_file(svgFilePath.c_str(), &error);
    if (!handle) {
        std::string err = error ? error->message : "Erreur inconnue chargement SVG.";
        if (error) g_error_free(error);
        throw std::runtime_error("rsvg_handle_new_from_file: " + err);
    }
    RsvgDimensionData dims;
    rsvg_handle_get_dimensions(handle, &dims); // Déprécié
    baseSVGWidth = dims.width;
    baseSVGHeight = dims.height;

    outWidth  = std::max(1, static_cast<int>(dims.width  * zoom));
    outHeight = std::max(1, static_cast<int>(dims.height * zoom));

    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, outWidth, outHeight);
    if (!surface) {
        g_object_unref(handle);
        throw std::runtime_error("Erreur création surface Cairo.");
    }
    cairo_t *cr = cairo_create(surface);
    if (!cr) {
        cairo_surface_destroy(surface);
        g_object_unref(handle);
        throw std::runtime_error("Erreur création contexte Cairo.");
    }
    cairo_scale(cr, zoom, zoom);
    if (!rsvg_handle_render_cairo(handle, cr)) {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        g_object_unref(handle);
        throw std::runtime_error("Erreur rendu SVG avec Cairo.");
    }
    cairo_surface_flush(surface);
    unsigned char *data = cairo_image_surface_get_data(surface);
    if (!data) {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        g_object_unref(handle);
        throw std::runtime_error("Erreur accès données Cairo.");
    }
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, outWidth, outHeight, 0,
                 GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_object_unref(handle);
    return texture;
}

// =============================
// Programme principal
// =============================
int main()
{
    if (!glfwInit()) {
        fprintf(stderr, "Erreur init GLFW\n");
        return -1;
    }
    GLFWwindow* window = glfwCreateWindow(1280, 720, "AST Viewer", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (glewInit() != GLEW_OK) {
        fprintf(stderr, "Erreur init GLEW\n");
        return -1;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Buffer de code (identique à la version précédente)
    static char code_buffer[8192] = R"(print("Hello World"))";
    std::string lastCode = code_buffer;
    double lastChangeTime = 0.0;
    bool codeChanged = false;
    // Logs : on accumule désormais la sortie d'erreur
    static std::string logOutput = "";

    // Options pour les flags
    static bool optionX = false;
    static bool optionB = false;
    static bool optionR = false;
    static bool optionE = false;

    // Fichier SVG final
    const std::string svgPath = "/tmp/ast.svg";

    // Variables pour zoom, pan et dimensions
    static float zoom = 1.0f;
    zoom = std::clamp(zoom, 0.05f, 3.5f);  // slider va de 0.05 à 3.5
    static float offsetX = 0.0f;
    static float offsetY = 0.0f;
    static GLuint svgTexture = 0;
    static int imgWidth = 0, imgHeight = 0;
    // Fenêtre de logs
    static bool showLogs = false;

    // Fonction de re-rasterisation
    auto reRenderSVG = [&]() {
        if (svgTexture != 0) {
            glDeleteTextures(1, &svgTexture);
            svgTexture = 0;
        }
        try {
            svgTexture = render_svg_with_librsvg(svgPath, zoom, imgWidth, imgHeight);
        } catch (const std::runtime_error &e) {
            fprintf(stderr, "Erreur rendu SVG: %s\n", e.what());
        }
    };

    // Fonction de compilation
    auto doCompile = [&]() {
        CompilerOutput compOut = run_compiler_with_input(code_buffer, optionX, optionB, optionR, optionE);
        // On ajoute les erreurs (stderr) aux logs, sans effacer les précédentes
        logOutput += compOut.err + "\n";
        if (!compOut.out.empty() && generate_svg_from_dot(compOut.out)) {
            reRenderSVG();
        } else {
            fprintf(stderr, "Compilation ou génération SVG échouée, on conserve l'ancienne image.\n");
        }
    };

    doCompile();

    // Boucle principale
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Barre de menu en haut
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::Button("Compiler")) {
                doCompile();
            }
            ImGui::SameLine();
            if (ImGui::Button("Open File")) {
                const char *filterPatterns[1] = { "*.tig;*.tih" };
                const char *selectedFile = tinyfd_openFileDialog("Open File", "", 1, filterPatterns, NULL, 0);
                if (selectedFile) {
                    std::ifstream file(selectedFile);
                    if (file) {
                        std::stringstream ss;
                        ss << file.rdbuf();
                        std::string content = ss.str();
                        strncpy(code_buffer, content.c_str(), sizeof(code_buffer)-1);
                        code_buffer[sizeof(code_buffer)-1] = '\0';
                        doCompile();
                    }
                }
            }
            if (ImGui::BeginMenu("Options")) {
                bool tmpX = optionX;
                if (ImGui::MenuItem("Option -X", nullptr, &tmpX) && (tmpX != optionX)) {
                    optionX = tmpX;
                    doCompile();
                }
                bool tmpB = optionB;
                if (ImGui::MenuItem("Option -b", nullptr, &tmpB) && (tmpB != optionB)) {
                    optionB = tmpB;
                    doCompile();
                }
                bool tmpR = optionR;
                if (ImGui::MenuItem("Option -r", nullptr, &tmpR) && (tmpR != optionR)) {
                    optionR = tmpR;
                    doCompile();
                }
                bool tmpE = optionE;
                if (ImGui::MenuItem("Option -e", nullptr, &tmpE) && (tmpE != optionE)) {
                    optionE = tmpE;
                    doCompile();
                }
                ImGui::EndMenu();
            }
            ImGui::SameLine();
            if (ImGui::Button("Logs"))
                showLogs = !showLogs;
            ImGui::EndMainMenuBar();
        }

        // Détection des modifications dans le code
        if (strcmp(code_buffer, lastCode.c_str()) != 0) {
            lastCode = code_buffer;
            codeChanged = true;
            lastChangeTime = ImGui::GetTime();
        }
        double now = ImGui::GetTime();
        if (codeChanged && (now - lastChangeTime) > COMPILE_DELAY) {
            codeChanged = false;
            doCompile();
        }

        // Fenêtre principale occupant tout l'espace sous la barre de menu
        ImVec2 dispSize = io.DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 20));
        ImGui::SetNextWindowSize(ImVec2(dispSize.x, dispSize.y - 20));
        ImGui::Begin("MainArea", nullptr,
                     ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoResize   |
                     ImGuiWindowFlags_NoMove);

        // Colonne gauche : éditeur de code (reste comme avant)
        ImGui::Columns(2, "SplitColumns", true);
        {
            float halfWidth = ImGui::GetWindowWidth() * 0.5f;
            ImGui::SetColumnWidth(0, halfWidth);
            ImGui::Text("Éditeur de code :");
            ImGui::InputTextMultiline("##code", code_buffer, sizeof(code_buffer),
                                      ImVec2(halfWidth - 10.0f, ImGui::GetWindowHeight() - 60.0f));
        }
        ImGui::NextColumn();
        {
            // Zone d'aperçu AST dans un Child
            ImGui::BeginChild("ASTImageArea", ImVec2(0, 0), true);
            ImVec2 childSize = ImGui::GetContentRegionAvail();
            float sliderWidth = 30.0f;
            // La zone pour l'image occupe tout sauf la largeur du slider et un petit espacement
            float imageAreaWidth = childSize.x - sliderWidth - 10.0f;
            
            // Zone pour l'image
            ImGui::BeginChild("ImageArea", ImVec2(imageAreaWidth, childSize.y), true);
            // Gestion du pan via le drag sur l'image
            if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(0)) {
                ImVec2 dragDelta = ImGui::GetMouseDragDelta(0);
                offsetX += dragDelta.x;
                offsetY += dragDelta.y;
                ImGui::ResetMouseDragDelta(0);
            }
            ImGui::Text("Zoom: %.2fx", zoom);
            ImVec2 curPos = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(curPos.x + offsetX, curPos.y + offsetY));
            if (svgTexture != 0) {
                ImGui::Image((ImTextureID)svgTexture, ImVec2((float)imgWidth, (float)imgHeight));
            } else {
                ImGui::Text("Aucune image à afficher.");
            }
            ImGui::EndChild(); // Fin de la zone ImageArea

            // Zone pour le slider vertical et le bouton Reset
            ImGui::SameLine();
            ImGui::BeginGroup();
            // Slider vertical placé avec une hauteur égale à la zone d'image moins 35 px (pour le bouton Reset)
            if (ImGui::VSliderFloat("##ZoomSlider", ImVec2(sliderWidth, childSize.y - 35.0f), &zoom, 0.05f, 3.5f, "Zoom: %.1fx")) {
                reRenderSVG();
            }
            // Bouton Reset juste en dessous du slider
            if (ImGui::Button("Reset", ImVec2(sliderWidth, 30))) {
                offsetX = 0;
                offsetY = 0;
                // Calculer le zoom optimal pour afficher l'image entière dans la zone d'image
                float availableWidth = imageAreaWidth;
                float availableHeight = childSize.y;
                if (baseSVGWidth > 0 && baseSVGHeight > 0 && availableWidth > 0 && availableHeight > 0) {
                    float newZoom = std::min(availableWidth / (float)baseSVGWidth,
                                             availableHeight / (float)baseSVGHeight);
                    zoom = std::clamp(newZoom, 0.05f, 3.5f);
                    reRenderSVG();
                } else {
                    fprintf(stderr, "Reset impossible : dimensions invalides (baseSVGWidth=%d, baseSVGHeight=%d, available=(%.1f, %.1f))\n",
                            baseSVGWidth, baseSVGHeight, availableWidth, availableHeight);
                }
            }
            ImGui::EndGroup();
            ImGui::EndChild(); // Fin ASTImageArea
        }

        ImGui::End(); // Fin MainArea

        // Fenêtre de logs en bas (extensible à 1/4 de la hauteur) affichant la sortie d'erreur
        if (showLogs) {
            ImGui::SetNextWindowPos(ImVec2(0, dispSize.y * 0.75f));
            ImGui::SetNextWindowSize(ImVec2(dispSize.x, dispSize.y * 0.25f));
            ImGui::Begin("Logs", &showLogs);
            ImGui::TextWrapped("%s", logOutput.c_str());
            ImGui::End();
        }

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    if (svgTexture != 0)
        glDeleteTextures(1, &svgTexture);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
