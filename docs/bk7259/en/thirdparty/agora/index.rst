Agora
=================================


:link_to_translation:`zh_CN:[中文]`

1. Account
----------------

    This section mainly explains account registration, project creation, APPID acquisition and other operations.

    Since Agora's web settings may change over time, it is recommended that you combine Agora's official documentation for actual operations.

        https://docs.agora.io/cn/Agora%20Platform/manage_projects


1.1 Account Registration
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

	Please log in to ``sso2.agora.io`` to register and log in using email and phone number.

1.2 Create Project
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    - Log in and enter the console

    - In Project Management -> Project List, click ``Create a Project`` on the right

    - In the Action column, click ``Configure`` for further configuration.

        Note that Agora may automatically redirect to Chinese or English website based on IP address. The configuration interface of Agora Chinese and English websites differs significantly.

        English website, i.e., ``sso2.agora.io``. For demo stage, it is recommended to always set it to "Testing".

        Chinese website, i.e., ``shengwang.cn``. Fill in Project Name/Scenario Label according to your situation. Note the authentication mechanism here. For demo stage, please use debug mode. Security mode requires business confirmation with Agora before use.


1.3 APPID
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

	- Please log in to ``sso2.agora.io`` using Agora account and enter the console

	- In Project Management -> Project List, you can see the App ID list column. Please click the Copy button above to get the App ID of the corresponding Project

    - In subsequent documents, AGORA_APPID is used to refer to App ID.

1.4 RESTful
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

	- Please log in to ``sso2.agora.io`` using Agora account and enter the console

	- In the console -> Click, right side personal avatar position -> RESTful API -> Add key

    - In subsequent documents, AGORA_RESTFUL_TOKEN is used to refer to RESTful API key.

2. AI Agent
----------------

   This article uses REST API, using curl commands, to control starting Agora AI Agent on PC side
   
   For detailed parameter instructions, please refer to the original document provided by Agora ``/docs/thirdparty/agora_ai_agent``


2.1 Open AI
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,


- Start Open AI Agent

Please replace the following strings before use:

    * AGORA_APPID, see section 1.3.

    * AGORA_RESTFUL_TOKEN, see section 1.4.

    * CHANNEL_NAME, custom string (recommended to use English letters and numbers combination, do not use special characters), consistent with the channel name set on the BK7259 robot device.

    * OPEN_TOKEN, Token obtained from Open AI official website backend.

    * AZURE_TTS_KEY, Azure TTS account and key applied from Microsoft official website.

    * AZURE_TTS_REGION, server domain corresponding to Azure TTS account.

::

        curl --location --request POST 'https://api.agora.io/api/conversational-ai-agent/v2/projects/AGORA_APPID/join' --header 'Content-Type: application/json' --header 'Authorization: Basic AGORA_RESTFUL_TOKEN' --data-raw '{
            "name": "CHANNEL_NAME",
            "properties": {
                "channel": "CHANNEL_NAME",
                "token": "",
                "agent_rtc_uid": "1234",
                "remote_rtc_uids": [
                    "123"
                ],
                "advanced_features": {
                    "enable_bhvs": true,
                    "enable_aivad": false
                },
                "parameters": {
                    "enable_dump": true,
                    "output_audio_codec": "G722"
                },
                "enable_string_uid": false,
                "idle_timeout": 0,
                "llm": {
                    "url": "https://api.openai.com/v1/chat/completions",
                    "api_key": "OPEN_TOKEN",
                    "system_messages": [
                    {
                        "role": "system",
                        "content": "You are a helpful chatbot."
                    }
                    ],
                    "max_history": 10,
                    "greeting_message": "Merry Christmas, how can I assist you today?",
                    "failure_message": "I am sorry!",
                    "params": {
                        "model": "gpt-4o-mini"
                    }
                },
                "tts": {
                    "vendor": "microsoft",
                    "params": {
                        "key": "AZURE_TTS_KEY",
                        "region": "AZURE_TTS_REGION",
                        "voice_name": "en-US-AndrewMultilingualNeural",
                        "vol": 10
                    }
                },
                "asr": {
                    "language": "en-US",
                    "vendor": "tencent"
                }
            }
        }'

After successful startup, you will receive the following return message:

    * AGORA_AGENT_ID, the ID value automatically assigned by the system. Each CHANNEL_NAME has a unique and different value.

        Can be used as input parameter for subsequent Agent startup.

::

    {
        "agent_id":"AGORA_AGENT_ID","create_ts":1739609818,"status":"RUNNING"
    }

- Stop Open AI Agent

Please replace the following strings before use:

    * AGORA_APPID, see section 1.3.

    * AGORA_RESTFUL_TOKEN, see section 1.4.

    * AGORA_AGENT_ID, the value obtained after starting AI Agent, see above.

::

    curl --location --request POST 'https://api.agora.io/api/conversational-ai-agent/v2/projects/AGORA_APPID/agents/AGORA_AGENT_ID/leave' --header 'Authorization: Basic AGORA_RESTFUL_TOKEN'



2.2 Doubao
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,


- Start Doubao AI Agent

Please replace the following strings before use:

    * AGORA_APPID, see section 1.3.

    * AGORA_RESTFUL_TOKEN, see section 1.4.

    * CHANNEL_NAME, custom string (recommended to use English letters and numbers combination, do not use special characters), consistent with the channel name set on the BK7259 robot device.

    * VOLC_API_KEY, Token obtained from VolcEngine ``volcengine.com`` backend.

    * VOLC_MODEL_ID, is the access point name ID obtained from online inference, i.e., the AI model ID. For demo, we used Doubao-pro-32k as the audio model.

        Creation method:

                After logging in to VolcEngine, find the Online Inference button in the left VolcEngine Ark, click to enter the online inference page.

                Then click, create inference access point, select Doubao-pro-32k as the audio model, you can also choose other models suitable for you.

                Finally, in the returned page, you can see VOLC_MODEL_ID under the model name.

    * BYTEDANCE_TTS_APPID, ByteDance TTS account and Token applied from VolcEngine official website. For application process, see reference links at the end.

    * BYTEDANCE_TTS_TOKEN, Token corresponding to ByteDance TTS account.

::

        curl --location --request POST 'https://api.agora.io/cn/api/conversational-ai-agent/v2/projects/AGORA_APPID/join' --header 'Content-Type: application/json' --header 'Authorization: Basic AGORA_RESTFUL_TOKEN' --data-raw '{
            "name": "CHANNEL_NAME",
            "properties": {
                "channel": "CHANNEL_NAME",
                "token": "",
                "agent_rtc_uid": "1234",
                "remote_rtc_uids": [
                    "123"
                ],
                "advanced_features": {
                    "enable_bhvs": true,
                    "enable_aivad": false
                },
                "parameters": {
                    "enable_dump": true,
                    "output_audio_codec": "G722"
                },
                "enable_string_uid": false,
                "idle_timeout": 0,
                "llm": {
                    "url": "https://ark.cn-beijing.volces.com/api/v3/chat/completions",
                    "api_key": "VOLC_API_KEY",
                    "system_messages": [
                    {
                        "role": "system",
                        "content": "你是一个有礼貌的AI助理。"
                    }
                    ],
                    "max_history": 10,
                    "greeting_message": "新春快乐，有什么可以帮您?",
                    "failure_message": "很抱歉.",
                    "params": {
                        "model": "VOLC_MODEL_ID"
                    }
                },
                "tts": {
                    "vendor": "bytedance",
                    "params": {
                        "token": "BYTEDANCE_TTS_TOKEN",
                        "app_id": "BYTEDANCE_TTS_APPID",
                        "cluster": "volcano_tts",
                        "speed_ratio": 1.0,
                        "volume_ratio": 0.6,
                        "pitch_ratio": 1.0,
                        "emotion": "happy"
                    }
                },
                "asr": {
                    "language": "zh-CN",
                    "vendor": "tencent"
                }
            }
        }'

After successful startup, you will receive the following return message:

    * AGORA_AGENT_ID, the ID value automatically assigned by the system. Each CHANNEL_NAME has a unique and different value.

        Can be used as input parameter for subsequent Agent startup.

::

    {
        "agent_id":"AGORA_AGENT_ID","create_ts":1739609818,"status":"RUNNING"
    }

- Stop Doubao AI Agent

Please replace the following strings before use:

    * AGORA_APPID, see section 1.3.

    * AGORA_RESTFUL_TOKEN, see section 1.4.

    * AGORA_AGENT_ID, the value obtained after starting AI Agent, see above.

::

    curl --location --request POST 'https://api.agora.io/cn/api/conversational-ai-agent/v2/projects/AGORA_APPID/agents/AGORA_AGENT_ID/leave' --header 'Authorization: Basic AGORA_RESTFUL_TOKEN'



3. Reference Links
--------------------

	Agora reference documentation: https://docs.agora.io/cn/Agora%20Platform/manage_projects?platform=Android

	Agora APPID application link: https://sso2.agora.io/cn/v5/login?_gl=1%2ardr355%2a_ga%2aMzkyNDM4ODYyLjE2NzM1MTM3MTU.%2a_ga_BFVGG7E02W%2aMTY3ODg1MjM0My4xMi4wLjE2Nzg4NTIzNDYuMC4wLjA.

	Agora AI Agent guide: ``/docs/thirdparty/agora_ai_agent``

	ByteDance TTS account application: https://www.volcengine.com/docs/6561/163043

