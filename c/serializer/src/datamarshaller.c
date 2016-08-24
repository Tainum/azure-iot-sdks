// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdlib.h> /*for free*/
#ifdef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif
#include "azure_c_shared_utility/gballoc.h"

#include <stdbool.h>
#include "datamarshaller.h"
#include "azure_c_shared_utility/crt_abstractions.h"
#include "schema.h"
#include "jsonencoder.h"
#include "agenttypesystem.h"
#include "azure_c_shared_utility/xlogging.h"
#include "parson.h"
#include "azure_c_shared_utility/vector.h"

DEFINE_ENUM_STRINGS(DATA_MARSHALLER_RESULT, DATA_MARSHALLER_RESULT_VALUES);

#define LOG_DATA_MARSHALLER_ERROR \
    LogError("(result = %s)", ENUM_TO_STRING(DATA_MARSHALLER_RESULT, result));

typedef struct DATA_MARSHALLER_HANDLE_DATA_TAG
{
    SCHEMA_MODEL_TYPE_HANDLE ModelHandle;
    bool IncludePropertyPath;
} DATA_MARSHALLER_HANDLE_DATA;

static int NoCloneFunction(void** destination, const void* source)
{
    *destination = (void*)source;
    return 0;
}

static void NoFreeFunction(void* value)
{
    (void)value;
}

DATA_MARSHALLER_HANDLE DataMarshaller_Create(SCHEMA_MODEL_TYPE_HANDLE modelHandle, bool includePropertyPath)
{
    DATA_MARSHALLER_HANDLE_DATA* result;

    /*Codes_SRS_DATA_MARSHALLER_99_019:[ DataMarshaller_Create shall return NULL if any argument is NULL.]*/
    if (
        (modelHandle == NULL) 
        )
    {
        result = NULL;
        LogError("(result = %s)", ENUM_TO_STRING(DATA_MARSHALLER_RESULT, DATA_MARSHALLER_INVALID_ARG));
    }
    else if ((result = (DATA_MARSHALLER_HANDLE_DATA*)malloc(sizeof(DATA_MARSHALLER_HANDLE_DATA))) == NULL)
    {
        /* Codes_SRS_DATA_MARSHALLER_99_048:[On any other errors not explicitly specified, DataMarshaller_Create shall return NULL.] */
        result = NULL;
        LogError("(result = %s)", ENUM_TO_STRING(DATA_MARSHALLER_RESULT, DATA_MARSHALLER_ERROR));
    }
    else
    {
        /*everything ok*/
        /*Codes_SRS_DATA_MARSHALLER_99_018:[ DataMarshaller_Create shall create a new DataMarshaller instance and on success it shall return a non NULL handle.]*/
        result->ModelHandle = modelHandle;
        result->IncludePropertyPath = includePropertyPath;
    }
    return result;
}

void DataMarshaller_Destroy(DATA_MARSHALLER_HANDLE dataMarshallerHandle)
{
    /* Codes_SRS_DATA_MARSHALLER_99_024:[ When called with a NULL handle, DataMarshaller_Destroy shall do nothing.] */
    if (dataMarshallerHandle != NULL)
    {
        /* Codes_SRS_DATA_MARSHALLER_99_022:[ DataMarshaller_Destroy shall free all resources associated with the dataMarshallerHandle argument.] */
        DATA_MARSHALLER_HANDLE_DATA* dataMarshallerInstance = (DATA_MARSHALLER_HANDLE_DATA*)dataMarshallerHandle;
        free(dataMarshallerInstance);
    }
}

DATA_MARSHALLER_RESULT DataMarshaller_SendData(DATA_MARSHALLER_HANDLE dataMarshallerHandle, size_t valueCount, const DATA_MARSHALLER_VALUE* values, unsigned char** destination, size_t* destinationSize)
{
    DATA_MARSHALLER_HANDLE_DATA* dataMarshallerInstance = (DATA_MARSHALLER_HANDLE_DATA*)dataMarshallerHandle;
    DATA_MARSHALLER_RESULT result;
    MULTITREE_HANDLE treeHandle;

    /* Codes_SRS_DATA_MARSHALLER_99_034:[All argument checks shall be performed before calling any other modules.] */
    /* Codes_SRS_DATA_MARSHALLER_99_004:[ DATA_MARSHALLER_INVALID_ARG shall be returned when the function has detected an invalid parameter (NULL) being passed to the function.] */
    if ((values == NULL) ||
        (dataMarshallerHandle == NULL) ||
        (destination == NULL) ||
        (destinationSize == NULL) ||
        /* Codes_SRS_DATA_MARSHALLER_99_033:[ DATA_MARSHALLER_INVALID_ARG shall be returned if the valueCount is zero.] */
        (valueCount == 0))
    {
        result = DATA_MARSHALLER_INVALID_ARG;
        LOG_DATA_MARSHALLER_ERROR
    }
    else
    {
        size_t i;
        bool includePropertyPath = dataMarshallerInstance->IncludePropertyPath;
        /* VS complains wrongly that result is not initialized */
        result = DATA_MARSHALLER_ERROR;

        for (i = 0; i < valueCount; i++)
        {
            if ((values[i].PropertyPath == NULL) ||
                (values[i].Value == NULL))
            {
                /*Codes_SRS_DATA_MARSHALLER_99_007:[ DATA_MARSHALLER_INVALID_MODEL_PROPERTY shall be returned when any of the items in values contain invalid data]*/
                result = DATA_MARSHALLER_INVALID_MODEL_PROPERTY;
                LOG_DATA_MARSHALLER_ERROR
                break;
            }

            if ((!dataMarshallerInstance->IncludePropertyPath) &&
                (values[i].Value->type == EDM_COMPLEX_TYPE_TYPE) &&
                (valueCount > 1))
            {
                /* Codes_SRS_DATAMARSHALLER_01_002: [If the includePropertyPath argument passed to DataMarshaller_Create was false and the number of values passed to SendData is greater than 1 and at least one of them is a struct, DataMarshaller_SendData shall fallback to  including the complete property path in the output JSON.] */
                includePropertyPath = true;
            }
        }

        if (i == valueCount)
        {
            /* Codes_SRS_DATA_MARSHALLER_99_037:[DataMarshaller shall store as MultiTree the data to be encoded by the JSONEncoder module.] */
            if ((treeHandle = MultiTree_Create(NoCloneFunction, NoFreeFunction)) == NULL)
            {
                /* Codes_SRS_DATA_MARSHALLER_99_035:[DATA_MARSHALLER_MULTITREE_ERROR shall be returned in case any MultiTree API call fails.] */
                result = DATA_MARSHALLER_MULTITREE_ERROR;
                LOG_DATA_MARSHALLER_ERROR
            }
            else
            {
                size_t j;
                result = DATA_MARSHALLER_OK; /* addressing warning in VS compiler */
                /* Codes_SRS_DATA_MARSHALLER_99_038:[For each pair in the values argument, a string : value pair shall exist in the JSON object in the form of propertyName : value.] */
                for (j = 0; j < valueCount; j++)
                {
                    if ((includePropertyPath == false) && (values[j].Value->type == EDM_COMPLEX_TYPE_TYPE))
                    {
                        size_t k;

                        /* Codes_SRS_DATAMARSHALLER_01_001: [If the includePropertyPath argument passed to DataMarshaller_Create was false and only one struct is being sent, the relative path of the value passed to DataMarshaller_SendData - including property name - shall be ignored and the value shall be placed at JSON root.] */
                        for (k = 0; k < values[j].Value->value.edmComplexType.nMembers; k++)
                        {
                            /* Codes_SRS_DATAMARSHALLER_01_004: [In this case the members of the struct shall be added as leafs into the MultiTree, each leaf having the name of the struct member.] */
                            if (MultiTree_AddLeaf(treeHandle, values[j].Value->value.edmComplexType.fields[k].fieldName, (void*)values[j].Value->value.edmComplexType.fields[k].value) != MULTITREE_OK)
                            {
                                break;
                            }
                        }

                        if (k < values[j].Value->value.edmComplexType.nMembers)
                        {
                            /* Codes_SRS_DATA_MARSHALLER_99_035:[DATA_MARSHALLER_MULTITREE_ERROR shall be returned in case any MultiTree API call fails.] */
                            result = DATA_MARSHALLER_MULTITREE_ERROR;
                            LOG_DATA_MARSHALLER_ERROR
                            break;
                        }
                    }
                    else
                    {
                        /* Codes_SRS_DATA_MARSHALLER_99_039:[ If the includePropertyPath argument passed to DataMarshaller_Create was true each property shall be placed in the appropriate position in the JSON according to its path in the model.] */
                        if (MultiTree_AddLeaf(treeHandle, values[j].PropertyPath, (void*)values[j].Value) != MULTITREE_OK)
                        {
                            /* Codes_SRS_DATA_MARSHALLER_99_035:[DATA_MARSHALLER_MULTITREE_ERROR shall be returned in case any MultiTree API call fails.] */
                            result = DATA_MARSHALLER_MULTITREE_ERROR;
                            LOG_DATA_MARSHALLER_ERROR
                            break;
                        }
                    }

                }

                if (j == valueCount)
                {
                    STRING_HANDLE payload = STRING_new();
                    if (payload == NULL)
                    {
                        result = DATA_MARSHALLER_ERROR;
                        LOG_DATA_MARSHALLER_ERROR
                    }
                    else
                    {
                        if (JSONEncoder_EncodeTree(treeHandle, payload, (JSON_ENCODER_TOSTRING_FUNC)AgentDataTypes_ToString) != JSON_ENCODER_OK)
                        {
                            /* Codes_SRS_DATA_MARSHALLER_99_027:[ DATA_MARSHALLER_JSON_ENCODER_ERROR shall be returned when JSONEncoder returns an error code.] */
                            result = DATA_MARSHALLER_JSON_ENCODER_ERROR;
                            LOG_DATA_MARSHALLER_ERROR
                        }
                        else
                        {
                            /*Codes_SRS_DATAMARSHALLER_02_007: [DataMarshaller_SendData shall copy in the output parameters *destination, *destinationSize the content and the content length of the encoded JSON tree.] */
                            size_t resultSize = STRING_length(payload);
                            unsigned char* temp = malloc(resultSize);
                            if (temp == NULL)
                            {
                                /*Codes_SRS_DATA_MARSHALLER_99_015:[ DATA_MARSHALLER_ERROR shall be returned in all the other error cases not explicitly defined here.]*/
                                result = DATA_MARSHALLER_ERROR;
                                LOG_DATA_MARSHALLER_ERROR;
                            }
                            else
                            {
                                memcpy(temp, STRING_c_str(payload), resultSize);
                                *destination = temp;
                                *destinationSize = resultSize;
                                result = DATA_MARSHALLER_OK;
                            }
                        }
                        STRING_delete(payload);
                    }
                } /* if (j==valueCount)*/
                MultiTree_Destroy(treeHandle);
            } /* MultiTree_Create */
        }
    }

    return result;
}

DATA_MARSHALLER_RESULT DataMarshaller_SendData_ReportedProperties(DATA_MARSHALLER_HANDLE dataMarshallerHandle, VECTOR_HANDLE values, unsigned char** destination, size_t* destinationSize)
{
    DATA_MARSHALLER_RESULT result;
    /*this function builds a parson object*/
    if (
        (dataMarshallerHandle == NULL) ||
        (values == NULL) ||
        (destination == NULL) ||
        (destinationSize == NULL)
        )
    {
        LogError("invalid argument DATA_MARSHALLER_HANDLE dataMarshallerHandle=%p, VECTOR_HANDLE values=%p, unsigned char** destination=%p, size_t* destinationSize=%p",
            dataMarshallerHandle,
            values,
            destination,
            destinationSize);
        result = DATA_MARSHALLER_INVALID_ARG;
    }
    else
    {
        JSON_Value* json = json_value_init_object();
        if (json == NULL)
        {
            LogError("unable to json_value_init_object");
            result = DATA_MARSHALLER_ERROR;
        }
        else
        {
            JSON_Object* jsonObject = json_object(json);
            if (jsonObject == NULL)
            {
                LogError("unable to json_object");
                json_value_free(json);
                result = DATA_MARSHALLER_ERROR;
            }
            else
            {
                size_t i;
                size_t nReportedProperties = VECTOR_size(values), nProcessedProperties = 0;

                for (i = 0;i < nReportedProperties; i++)
                {
                    DATA_MARSHALLER_VALUE* v = *(DATA_MARSHALLER_VALUE**)VECTOR_element(values, i);
                    STRING_HANDLE s = STRING_new();
                    if (s == NULL)
                    {
                        LogError("unable to STRING_new");
                        result = DATA_MARSHALLER_ERROR;
                        i = nReportedProperties;/*forces loop to break*/
                    }
                    else
                    {
                        if (AgentDataTypes_ToString(s, v->Value) != AGENT_DATA_TYPES_OK)
                        {
                            LogError("unable to AgentDataTypes_ToString");
                            result = DATA_MARSHALLER_ERROR;
                            i = nReportedProperties;/*forces loop to break*/
                            break;
                        }
                        else
                        {
                            JSON_Value * rightSide = json_parse_string(STRING_c_str(s));
                            if (rightSide == NULL)
                            {
                                LogError("unable to json_parse_string");
                                result = DATA_MARSHALLER_ERROR;
                                i = nReportedProperties; /*forces loop to break*/
                            }
                            else
                            {
                                char* leftSide;
                                if (mallocAndStrcpy_s(&leftSide, v->PropertyPath) != 0)
                                {
                                    LogError("unable to mallocAndStrcpy_s");
                                    result = DATA_MARSHALLER_ERROR;
                                    i = nReportedProperties;/*forces loop to break*/
                                }
                                else
                                {
                                    char *whereIsSlash;
                                    while ((whereIsSlash = strchr(leftSide, '/')) != NULL)
                                    {
                                        *whereIsSlash = '.';
                                    }

                                    if (json_object_dotset_value(jsonObject, leftSide, rightSide) != JSONSuccess)
                                    {
                                        LogError("unable to json_object_dotset_value");
                                        result = DATA_MARSHALLER_ERROR;
                                        i = nReportedProperties;/*forces loop to break*/
                                    }
                                    else
                                    {
                                        /*all is fine with this property... */
                                        nProcessedProperties++;
                                    }
                                    free(leftSide);
                                }
                                //json_value_free(rightSide);
                            }
                        }
                        STRING_delete(s);
                    }
                }

                if (nProcessedProperties != nReportedProperties)
                {
                    /*all properties have NOT been processed*/
                    /*return result as is*/
                }
                else
                {
                    char* temp = json_serialize_to_string_pretty(json);
                    if (temp == NULL)
                    {
                        LogError("unable to json_serialize_to_string_pretty ");
                        free(temp);
                        result = DATA_MARSHALLER_ERROR;
                    }
                    else
                    {
                        *destination = (unsigned char*)temp;
                        *destinationSize = strlen(temp);
                        result = DATA_MARSHALLER_OK;
                        /*all is fine... */
                    }
                }
            }
            result = DATA_MARSHALLER_OK;
        }
    }
    return result;
}
 