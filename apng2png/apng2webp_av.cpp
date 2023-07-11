#include <stdio.h>
#include <stdlib.h>
#include <png.h>
#include <webp/encode.h>
#include <webp/mux.h>

typedef struct {
    png_uint_32 width, height;
    png_uint_32 next_frame_delay_num, next_frame_delay_den;
    png_uint_32 num_frames;
    png_uint_32 num_plays;
    int *delays;
    unsigned char **frames;
} APNGData;

void user_read_data(png_structp png_ptr, png_bytep data, png_size_t length) {
    FILE *fp = (FILE *)png_get_io_ptr(png_ptr);
    fread(data, 1, length, fp);
}

int process_data(png_structp png_ptr, png_infop info_ptr, APNGData *apng_data) {
    //png_read_info(png_ptr, info_ptr);

    //设置解码的格式，统统转成BGRA格式
    png_set_expand(png_ptr);
    png_set_strip_16(png_ptr);
    png_set_gray_to_rgb(png_ptr);
    png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER);
    png_set_bgr(png_ptr);
    png_set_interlace_handling(png_ptr);
    png_read_update_info(png_ptr, info_ptr);

    // 分配内存存储帧数据和延时
    apng_data->width = png_get_image_width(png_ptr, info_ptr);
    apng_data->height = png_get_image_height(png_ptr, info_ptr);
    png_uint_32 m_nChannels = png_get_channels(png_ptr, info_ptr);
    apng_data->delays = (int *)malloc(apng_data->num_frames * sizeof(int));
    apng_data->frames = (unsigned char **)malloc(apng_data->num_frames * sizeof(unsigned char *));

    int width = apng_data->width;
    int height = apng_data->height;
    int num_frames = apng_data->num_frames;

    // 读取每一帧
    for (int i = 0; i < num_frames; i++) {
        printf("read PNG frames: [Total:%u-Now:%d]\n", num_frames,i+1);

        png_read_frame_head(png_ptr, info_ptr);
        apng_data->next_frame_delay_num = png_get_next_frame_delay_num(png_ptr, info_ptr);
        apng_data->next_frame_delay_den = png_get_next_frame_delay_den(png_ptr, info_ptr);

        // 为当前帧分配内存
        apng_data->frames[i] = (unsigned char *)malloc(width * height * 4);

        // 读取当前帧的行数据
        png_bytepp row_pointers = (png_bytepp)malloc(height * sizeof(png_bytep));
        for (int j = 0; j < height; ++j) {
            row_pointers[j] = apng_data->frames[i] + j * width * 4;
        }

        // 读取当前帧的像素数据
        png_read_image(png_ptr, row_pointers);

        // 清理
        free(row_pointers);
    }

    // 结束读取
    png_read_end(png_ptr, NULL);
    //png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    //fclose(fp);

    return 0;
}

int apng2webp(const char *input_file, const char *output_file) {
    FILE *in = fopen(input_file, "rb");
    if (!in) {
        fprintf(stderr, "Error opening input file: %s\n", input_file);
        return 1;
    }

    //char sig[8]={0};

    //fread(sig, 1, 8, in);
    //if (png_sig_cmp(sig, 0, 8) != 0) {

    //    fprintf(stderr, "Not a png image!\\n");
    //    fclose(in);
    //    return 1;
    //}

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        fprintf(stderr, "Error creating PNG read struct\n");
        fclose(in);
        return 1;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        fprintf(stderr, "Error creating PNG info struct\n");
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        fclose(in);
        return 1;
    }

    if (setjmp(png_jmpbuf(png_ptr))) {
        fprintf(stderr, "Error reading PNG\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(in);
        return 1;
    }

    //png_set_read_fn(png_ptr, in, user_read_data);
    //png_set_sig_bytes(png_ptr, 0);

    // 开始读取PNG/APNG文件
    png_init_io(png_ptr, in);
    png_read_info(png_ptr, info_ptr);
    
    APNGData apng_data;
    memset(&apng_data, 0, sizeof(APNGData));

    png_get_acTL(png_ptr, info_ptr, &apng_data.num_frames, &apng_data.num_plays);
    if (apng_data.num_frames <= 1) {
        fprintf(stderr, "Not an animated PNG\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        fclose(in);
        return 1;
    }

    printf("Number of frames: %u\n", apng_data.num_frames);
    printf("Number of plays: %u\n", apng_data.num_plays);

    process_data(png_ptr, info_ptr, &apng_data);

    fclose(in);

    WebPAnimEncoderOptions anim_config;
    WebPAnimEncoderOptionsInit(&anim_config);
    anim_config.anim_params.loop_count = 0;

    WebPAnimEncoder *enc = WebPAnimEncoderNew(apng_data.width, apng_data.height, &anim_config);
    if (!enc) {
        fprintf(stderr, "Error creating WebPAnimEncoder\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return 1;
    }

    int timestamp_ms = 0;
    for (png_uint_32 i = 0; i < apng_data.num_frames; i++) {
        WebPPicture *pic = apng_data.frames[i];
        WebPAnimEncoderAdd(enc, pic, timestamp_ms, NULL);
        WebPPictureFree(pic);

        if (i != apng_data.num_frames - 1) {
            int delay = apng_data.next_frame_delay_num;
            if (apng_data.next_frame_delay_den > 0) {
                delay = delay * 1000 / apng_data.next_frame_delay_den;
            }
            timestamp_ms += delay;
        }
    }
    WebPAnimEncoderAdd(enc, NULL, timestamp_ms, NULL);

    WebPData webp_data;
    WebPDataInit(&webp_data);
    WebPAnimEncoderAssemble(enc, &webp_data);

    FILE *out = fopen(output_file, "wb");
    if (!out) {
        fprintf(stderr, "Error opening output file: %s\n", output_file);
        WebPDataClear(&webp_data);
        WebPAnimEncoderDelete(enc);
        return 1;
    }

    fwrite(webp_data.bytes, 1, webp_data.size, out);
    fclose(out);

    WebPDataClear(&webp_data);
    WebPAnimEncoderDelete(enc);
    free(*apng_data.frames);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.apng> <output.webp>\n", argv[0]);
        return 1;
    }

    return apng2webp(argv[1], argv[2]);
}
