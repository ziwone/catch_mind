#pragma once
// Word bank data — edit this file to add/remove categories and words.
// Each category should have at least 10 words; ~40 is recommended.
// BODY category removed intentionally.

#include <string>
#include <vector>
#include <unordered_map>

inline void loadWordBank(std::unordered_map<std::string, std::vector<std::string>> &wb) {

    wb["ANIMAL"] = {
        "CAT", "DOG", "RABBIT", "TIGER", "ELEPHANT", "LION", "BEAR", "GIRAFFE",
        "PENGUIN", "DOLPHIN", "FOX", "WOLF", "HORSE", "SHEEP", "MONKEY", "CROCODILE",
        "EAGLE", "SHARK", "OWL", "PARROT", "FROG", "TURTLE", "SNAKE", "DEER",
        "ZEBRA", "GORILLA", "CHEETAH", "FLAMINGO", "KANGAROO", "PANDA",
        "OCTOPUS", "JELLYFISH", "SCORPION", "HEDGEHOG", "SQUIRREL", "RACCOON",
        "HAMSTER", "PEACOCK", "RHINO", "HIPPO"
    };

    wb["FRUIT"] = {
        "APPLE", "BANANA", "GRAPE", "ORANGE", "MELON", "PEACH", "STRAWBERRY",
        "WATERMELON", "PINEAPPLE", "CHERRY", "MANGO", "LEMON", "KIWI", "PEAR",
        "COCONUT", "BLUEBERRY", "RASPBERRY", "POMEGRANATE", "APRICOT", "PLUM",
        "PAPAYA", "GUAVA", "LYCHEE", "DRAGONFRUIT", "PASSIONFRUIT", "TANGERINE",
        "AVOCADO", "FIG", "DATE", "PERSIMMON",
        "STARFRUIT", "JACKFRUIT", "NECTARINE", "MULBERRY", "BLACKBERRY",
        "CRANBERRY", "GOOSEBERRY", "DURIAN", "QUINCE", "LIME"
    };

    wb["FOOD"] = {
        "PIZZA", "HAMBURGER", "SUSHI", "RAMEN", "TACO", "HOTDOG", "SANDWICH",
        "PASTA", "DONUT", "CAKE", "COOKIE", "POPCORN", "ICECREAM", "CURRY",
        "STEAK", "SALAD", "WAFFLE", "PANCAKE", "BURRITO", "DUMPLING",
        "CROISSANT", "BAGEL", "PRETZEL", "NACHOS", "PAELLA", "RISOTTO",
        "LASAGNA", "FALAFEL", "HUMMUS", "KEBAB",
        "BROWNIE", "CHEESECAKE", "MUFFIN", "CREPE", "OMELETTE", "QUICHE",
        "TEMPURA", "PHO", "BIBIMBAP", "GYOZA"
    };

    wb["OBJECT"] = {
        "CAR", "PHONE", "CHAIR", "CLOCK", "BOOK", "CUP", "UMBRELLA", "CAMERA",
        "GUITAR", "BICYCLE", "AIRPLANE", "ROCKET", "COMPASS", "TELESCOPE",
        "ROBOT", "KEYBOARD", "LAMP", "SCISSORS", "HAMMER", "SHOVEL",
        "LADDER", "MIRROR", "MAGNET", "MICROSCOPE", "PARACHUTE", "SUBMARINE",
        "CANDLE", "TROPHY", "ANCHOR", "CROWN",
        "SKATEBOARD", "HELMET", "BRIEFCASE", "BACKPACK", "HOURGLASS",
        "MAGNIFIER", "BALLOON", "LANTERN", "SAILBOAT", "WINDMILL"
    };

    wb["NATURE"] = {
        "MOUNTAIN", "RIVER", "OCEAN", "VOLCANO", "FOREST", "DESERT", "RAINBOW",
        "TORNADO", "SNOWFLAKE", "CLOUD", "ISLAND", "WATERFALL", "GLACIER",
        "CANYON", "LIGHTHOUSE", "CACTUS", "AURORA", "TSUNAMI", "GEYSER",
        "MARSH", "CLIFF", "DUNE", "MEADOW", "CORAL", "CAVE",
        "TUNDRA", "SAVANNA", "FJORD", "LAGOON", "PLATEAU",
        "STALACTITE", "HOT_SPRING", "TIDE", "CREEK", "ICEBERG",
        "BLIZZARD", "HURRICANE", "SAND_DUNE", "MANGROVE", "ATOLL"
    };

    wb["SPORT"] = {
        "SOCCER", "BASKETBALL", "BASEBALL", "TENNIS", "SWIMMING", "GOLF",
        "BOXING", "SKIING", "SURFING", "ARCHERY", "FENCING", "DIVING",
        "BOWLING", "CYCLING", "WRESTLING", "VOLLEYBALL", "RUGBY", "HOCKEY",
        "BADMINTON", "TABLE_TENNIS", "ROWING", "TRIATHLON", "GYMNASTICS",
        "MARATHON", "JUDO", "TAEKWONDO", "KARATE", "SHOOTING",
        "POLE_VAULT", "DISCUS",
        "CLIMBING", "SKATEBOARDING", "SNOWBOARDING", "EQUESTRIAN", "SAILING",
        "CANOE", "WEIGHTLIFTING", "CURLING", "LACROSSE", "CRICKET"
    };

    wb["PLACE"] = {
        "SCHOOL", "HOSPITAL", "AIRPORT", "MUSEUM", "LIBRARY", "STADIUM",
        "RESTAURANT", "HOTEL", "CASTLE", "TEMPLE", "FARM", "FACTORY",
        "MARKET", "THEATER", "BRIDGE", "TUNNEL", "LIGHTHOUSE", "BAKERY",
        "PHARMACY", "ZOO", "AQUARIUM", "CATHEDRAL", "PALACE", "PRISON",
        "SUBMARINE", "LIGHTHOUSE", "HARBOR", "VOLCANO", "PYRAMID", "IGLOO",
        "SKYSCRAPER", "COTTAGE", "MANSION", "BARN", "GREENHOUSE",
        "OBSERVATORY", "ARENA", "GARAGE", "CEMETERY", "FOUNTAIN"
    };
}
