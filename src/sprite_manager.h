#ifndef SPRITE_MANAGER_H
#define SPRITE_MANAGER_H

#include <glad/glad.h>

#include <array>
#include <cstddef>

#include "basic_transform.h"
#include "texture_atlas.h"
#include "vector_util.h"
#include <SFML/Graphics.hpp>
#include <SFML/OpenGL.hpp>

#define GL_SILENCE_DEPRECATION  // silences macOS OpenGL deprecation warnings

#define glCheck(expr)                                                                              \
    expr;                                                                                          \
    if (int e = glGetError()) {                                                                    \
        std::cerr << "OpenGL error: " << e << " in file " __FILE__ << ", line number " << __LINE__ \
                  << '\n';                                                                         \
        abort();                                                                                   \
    }

using SpriteID = size_t;

class SpriteManager {
public:
    SpriteManager(const sf::Vector2u& atlasSize, unsigned padding)
        : mVertices(sf::PrimitiveType::Triangles),
          mNumSprites{0},
          mAtlas(atlasSize, padding),
          mShader(std::filesystem::path{"main.vert"}, std::filesystem::path{"main.frag"}) {
        const GLuint program = mShader.getNativeHandle();
        mPositionAttribLocation = glGetAttribLocation(program, "position");
        mTexCoordsAttribLocation = glGetAttribLocation(program, "texCoords");
        mSpriteNumberAttribLocation = glGetAttribLocation(program, "spriteNumber");
        mTextureUniformLocation = glGetUniformLocation(program, "texture");
        mTransformsUniformLocation = glGetUniformLocation(program, "transforms");
        mNumTransformsUniformLocation = glGetUniformLocation(program, "numTransforms");

        glCheck(glGenBuffers(1, &mVerticesVBO));
        glCheck(glGenBuffers(1, &mSpriteNumberVBO));
        glCheck(glGenTextures(1, &mTransformsTexture));

        // Transforms are read back by exact index, so never interpolate between them.
        glCheck(glBindTexture(GL_TEXTURE_1D, mTransformsTexture));
        glCheck(glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
        glCheck(glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
        glCheck(glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    }

    ~SpriteManager() {
        glDeleteBuffers(1, &mVerticesVBO);
        glDeleteBuffers(1, &mSpriteNumberVBO);
        glDeleteTextures(1, &mTransformsTexture);
    }

    TextureID registerTexture(const std::string& filepath) {
        return mAtlas.registerTexture(filepath);
    }

    SpriteID createSprite(TextureID textureID) {
        const SpriteID id = mNumSprites++;
        mVertices.resize(mVertices.getVertexCount() + VERTICES_PER_SPRITE);
        mSpriteNumbers.resize(mSpriteNumbers.size() + VERTICES_PER_SPRITE, static_cast<float>(id));
        mTransforms.emplace_back();
        // New geometry and UVs are exactly what setTexture produces.
        setTexture(id, textureID);
        return id;
    }

    void setTexture(SpriteID sprite, TextureID newTexture) {
        const sf::Vector2f size(mAtlas.getSizePixels(newTexture));
        const sf::FloatRect uv = mAtlas.getBoundsUV(newTexture);

        // getBoundsUV reports a bottom-left UV origin, but an SFML texture stores image row 0
        // at v = 0, so flip v to match how the texture is laid out in memory.
        const float u0 = uv.position.x;
        const float u1 = uv.position.x + uv.size.x;
        const float vTop = 1.f - (uv.position.y + uv.size.y);
        const float vBottom = vTop + uv.size.y;

        // Quad corners in world units, going around the quad.
        const std::array<sf::Vector2f, 4> corners = {
            {sf::Vector2f{0.f, 0.f}, sf::Vector2f{size.x, 0.f}, sf::Vector2f{size.x, size.y},
             sf::Vector2f{0.f, size.y}}};
        const std::array<sf::Vector2f, 4> uvs = {{sf::Vector2f{u0, vBottom},
                                                  sf::Vector2f{u1, vBottom}, sf::Vector2f{u1, vTop},
                                                  sf::Vector2f{u0, vTop}}};
        // Two triangles: (0, 1, 2) and (0, 2, 3).
        const std::array<size_t, VERTICES_PER_SPRITE> order = {0, 1, 2, 0, 2, 3};

        for (size_t i = 0; i < VERTICES_PER_SPRITE; ++i) {
            sf::Vertex& vertex = mVertices[sprite * VERTICES_PER_SPRITE + i];
            vertex.position = corners[order[i]];
            vertex.texCoords = uvs[order[i]];
        }
    }

    // Batched rendering (part 1): transform on the CPU, then one SFML draw call.
    void draw(sf::RenderTarget& target, sf::RenderStates states) const {
        sf::VertexArray transformed(sf::PrimitiveType::Triangles);
        transformed.resize(mVertices.getVertexCount());
        for (size_t i = 0; i < mVertices.getVertexCount(); ++i) {
            transformed[i] = mVertices[i];
            transformed[i].position =
                mTransforms[i / VERTICES_PER_SPRITE].applyPoint(mVertices[i].position);
        }
        states.texture = &mAtlas.getTexture();
        states.shader = &mShader;
        target.draw(transformed, states);
    }

    // Batched rendering (part 2): one native OpenGL draw call, transforms applied on the GPU.
    void drawGL() {
        // Every transform becomes one RGBA texel of a 1D float texture.
        std::vector<float> transformData;
        transformData.reserve(mNumSprites * 4);
        for (const auto& transform : mTransforms) {
            transformData.insert(transformData.end(),
                                 {transform.scale.x, transform.scale.y, transform.translation.x,
                                  transform.translation.y});
        }
        glCheck(glActiveTexture(GL_TEXTURE1));
        glCheck(glBindTexture(GL_TEXTURE_1D, mTransformsTexture));
        glCheck(glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA32F_ARB, static_cast<GLsizei>(mNumSprites), 0,
                             GL_RGBA, GL_FLOAT, transformData.data()));

        // The atlas lives on texture unit 0.
        glCheck(glActiveTexture(GL_TEXTURE0));
        glCheck(glBindTexture(GL_TEXTURE_2D, mAtlas.getTexture().getNativeHandle()));

        glCheck(glUseProgram(mShader.getNativeHandle()));
        glCheck(glUniform1i(mTextureUniformLocation, 0));
        glCheck(glUniform1i(mTransformsUniformLocation, 1));
        glCheck(glUniform1i(mNumTransformsUniformLocation, static_cast<int>(mNumSprites)));

        glCheck(glBindBuffer(GL_ARRAY_BUFFER, mVerticesVBO));
        glCheck(
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(mVertices.getVertexCount() * sizeof(sf::Vertex)),
                         &mVertices[0], GL_STREAM_DRAW));
        glCheck(glVertexAttribPointer(
            mPositionAttribLocation, 2, GL_FLOAT, GL_FALSE, sizeof(sf::Vertex),
            reinterpret_cast<const void*>(offsetof(sf::Vertex, position))));
        glCheck(glVertexAttribPointer(
            mTexCoordsAttribLocation, 2, GL_FLOAT, GL_FALSE, sizeof(sf::Vertex),
            reinterpret_cast<const void*>(offsetof(sf::Vertex, texCoords))));

        glCheck(glBindBuffer(GL_ARRAY_BUFFER, mSpriteNumberVBO));
        glCheck(glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(mSpriteNumbers.size() * sizeof(float)),
                             mSpriteNumbers.data(), GL_STREAM_DRAW));
        glCheck(glVertexAttribPointer(mSpriteNumberAttribLocation, 1, GL_FLOAT, GL_FALSE,
                                      sizeof(float), nullptr));

        glCheck(glEnableVertexAttribArray(mPositionAttribLocation));
        glCheck(glEnableVertexAttribArray(mTexCoordsAttribLocation));
        glCheck(glEnableVertexAttribArray(mSpriteNumberAttribLocation));

        glCheck(glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mVertices.getVertexCount())));

        glCheck(glDisableVertexAttribArray(mPositionAttribLocation));
        glCheck(glDisableVertexAttribArray(mTexCoordsAttribLocation));
        glCheck(glDisableVertexAttribArray(mSpriteNumberAttribLocation));
    }

    BasicTransform& getTransform(SpriteID id) { return mTransforms[id]; }

    const TextureAtlas& getAtlas() const { return mAtlas; }

private:
    static constexpr size_t VERTICES_PER_SPRITE = 6;

    sf::VertexArray mVertices;
    std::vector<BasicTransform> mTransforms;
    size_t mNumSprites;
    TextureAtlas mAtlas;
    mutable sf::Shader mShader;

    // Use the below only in Batched rendering (part 2):
    std::vector<float> mSpriteNumbers;

    GLint mTextureUniformLocation;
    GLint mTransformsUniformLocation;
    GLint mNumTransformsUniformLocation;

    GLint mPositionAttribLocation;
    GLint mTexCoordsAttribLocation;
    GLint mSpriteNumberAttribLocation;

    GLuint mVerticesVBO;
    GLuint mSpriteNumberVBO;
    GLuint mTransformsTexture;
};

#endif  // SPRITE_MANAGER_H
