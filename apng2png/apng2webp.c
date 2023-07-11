#include <iostream>
#include <vector>
#include <png.h>
#include <webp/encode.h>
#include <webp/mux.h>
#include <string.h>

struct Frame {
    std::vector<uint8_t> rgba;
    int width;
    int height;
    int duration;  // Frame duration in milliseconds
};

bool load_apng(const char* filename, std::vector<Frame>& frames) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return false;
    }

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        std::cerr << "Error creating read struct" << std::endl;
        fclose(file);
        return false;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        std::cerr << "Error creating info struct" << std::endl;
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        fclose(file);
        return false;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        std::cerr << "Error during libpng init_io" << std::endl;
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(file);
        return false;
    }

    png_init_io(png_ptr, file);
    png_read_info(png_ptr, info_ptr);

    png_uint_32 width, height;
    int bit_depth, color_type;
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, NULL, NULL, NULL);

    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png_ptr);
    }

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    }

    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png_ptr);
    }

    if (bit_depth == 16) {
        png_set_strip_16(png_ptr);
    }

    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png_ptr);
    }

    png_set_filler(png_ptr, 0xFF, PNG_FILLER_AFTER);
    png_read_update_info(png_ptr, info_ptr);

    png_uint_32 num_frames = png_get_num_frames(png_ptr, info_ptr);
    frames.reserve(num_frames);

 for (png_uint_32 frame_num = 0; frame_num < num_frames; ++frame_num) {
        png_read_frame_head(png_ptr, info_ptr);

        png_uint_32 x_offset, y_offset, sub_width, sub_height;
        png_uint_16 delay_num, delay_den;
        png_byte dispose_op, blend_op;

        png_get_next_frame_fcTL(png_ptr, info_ptr, &sub_width, &sub_height, &x_offset, &y_offset,
                                &delay_num, &delay_den, &dispose_op, &blend_op);

        Frame frame;
        frame.width = width;
        frame.height = height;
        frame.duration = delay_num * 1000 / (delay_den ? delay_den : 1000);  // Convert to milliseconds
        frame.rgba.resize(width * height * 4, 0);

        std::vector<png_bytep> row_pointers(sub_height);

        for (png_uint_32 y = 0; y < sub_height; ++y) {
            row_pointers[y] = frame.rgba.data() + (y + y_offset) * width * 4 + x_offset * 4;
        }

        png_read_image(png_ptr, row_pointers.data());
        frames.push_back(frame);
    }

    fclose(file);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    return true;
}

bool save_webp(const char* filename, const std::vector<Frame>& frames) {
    if (frames.empty()) {
        std::cerr << "No frames to save" << std::endl;
        return false;
    }

    WebPAnimEncoderOptions enc_options;
    WebPAnimEncoderOptionsInit(&enc_options);
    enc_options.anim_params.loop_count = 0;  // Infinite loop

    WebPAnimEncoder* enc = WebPAnimEncoderNew(frames.front().width, frames.front().height, &enc_options);
    if (!enc) {
        std::cerr << "Error creating WebP anim encoder" << std::endl;
        return false;
    }

    for (size_t i = 0; i < frames.size(); ++i) {
        const Frame& frame = frames[i];

        WebPPicture picture;
        if (!WebPPictureInit(&picture)) {
            std::cerr << "Error initializing WebPPicture" << std::endl;
            WebPAnimEncoderDelete(enc);
            return false;
        }

        picture.width = frame.width;
        picture.height = frame.height;
        picture.use_argb = 1;
        picture.argb_stride = frame.width;

        if (!WebPPictureImportRGBA(&picture, &frame.rgba[0], frame.width * 4)) {
            std::cerr << "Error importing RGBA data to WebPPicture" << std::endl;
            WebPPictureFree(&picture);
            WebPAnimEncoderDelete(enc);
            return false;
        }

        if (!WebPAnimEncoderAdd(enc, &picture, frame.duration, NULL)) {
            std::cerr << "Error adding frame to WebP anim encoder"
                      << std::endl;
            WebPPictureFree(&picture);
            WebPAnimEncoderDelete(enc);
            return false;
        }

        WebPPictureFree(&picture);
    }

    WebPData webp_data;
    WebPDataInit(&webp_data);
    if (!WebPAnimEncoderAssemble(enc, &webp_data)) {
        std::cerr << "Error assembling WebP animation" << std::endl;
        WebPDataClear(&webp_data);
        WebPAnimEncoderDelete(enc);
        return false;
    }

    FILE* file = fopen(filename, "wb");
    if (!file) {
        std::cerr << "Error opening file: " << filename << std::endl;
        WebPDataClear(&webp_data);
        WebPAnimEncoderDelete(enc);
        return false;
    }

    fwrite(webp_data.bytes, webp_data.size, 1, file);
    fclose(file);

    WebPDataClear(&webp_data);
    WebPAnimEncoderDelete(enc);
    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " input.apng output.webp" << std::endl;
        return 1;
    }

    const char* input_filename = argv[1];
    const char* output_filename = argv[2];

    std::vector<Frame> frames;
    if (!load_apng(input_filename, frames)) {
        std::cerr << "Error loading APNG file: " << input_filename << std::endl;
        return 1;
    }

    if (!save_webp(output_filename, frames)) {
        std::cerr << "Error saving WebP file: " << output_filename << std::endl;
        return 1;
    }

    std::cout << "Converted " << input_filename << " to " << output_filename << std::endl;
    return 0;
}
