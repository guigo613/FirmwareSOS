/*This is tone file*/

const char* tone_uri[] = {
   "flash://tone/0_alerta1.mp3",
   "flash://tone/1_call_terminated.mp3",
   "flash://tone/2_calling.mp3",
   "flash://tone/3_no_answer.mp3",
   "flash://tone/4_ring.mp3",
};

int get_tone_uri_num()
{
    return sizeof(tone_uri) / sizeof(char *) - 1;
}
